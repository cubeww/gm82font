#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace {

constexpr uintptr_t kPreferredImageBase = 0x00400000;
constexpr uintptr_t kVaTextureCreateEmpty = 0x00620A68;
constexpr uintptr_t kVaTextureFree = 0x006210D0;
constexpr uintptr_t kVaTextureSlotsPointer = 0x0085B3C4;
constexpr uintptr_t kVaD3DDevicePointer = 0x00689570;
constexpr uintptr_t kVaDrawHalign = 0x0068864C;
constexpr uintptr_t kVaDrawValign = 0x00688650;

struct GlyphCacheEntry {
    int texture_id = -1;
    int width = 0;
    int height = 0;
    int bitmap_left = 0;
    int bitmap_top = 0;
    double advance_x = 0.0;
    bool has_bitmap = false;
};

struct FontFace {
    std::vector<std::uint8_t> file_data;
    FT_Face face = nullptr;
    std::unordered_map<std::uint32_t, GlyphCacheEntry> glyph_cache;
};

struct D3DLOCKED_RECT {
    INT Pitch;
    void* pBits;
};

struct GMTextureSlot {
    void* texture;
    int width;
    int height;
    int texture_width;
    int texture_height;
    int valid;
};

struct TextLayout {
    int width = 1;
    int height = 1;
    int line_height = 1;
    int baseline = 1;
    double origin_x = 1.0;
};

struct GMDrawVertex {
    float x;
    float y;
    float z;
    float rhw;
    std::uint32_t color;
    float u;
    float v;
};

struct TextDrawState {
    double wrap_width = -1.0;
    double line_sep = -1.0;
    double xscale = 1.0;
    double yscale = 1.0;
    double angle_degrees = 0.0;
    std::uint32_t c1 = 0x00FFFFFFu;
    std::uint32_t c2 = 0x00FFFFFFu;
    std::uint32_t c3 = 0x00FFFFFFu;
    std::uint32_t c4 = 0x00FFFFFFu;
    double alpha = 1.0;
    bool use_gradient = false;
};

struct ShapedGlyph {
    int texture_id = -1;
    int width = 0;
    int height = 0;
    int texture_width = 0;
    int texture_height = 0;
    double pen_x = 0.0;
    double advance_x = 0.0;
    double local_x = 0.0;
    double local_y = 0.0;
};

struct PreparedTextLine {
    std::string utf8;
    TextLayout layout;
    double advance_width = 0.0;
    std::vector<ShapedGlyph> glyphs;
};

std::mutex g_mutex;
std::unordered_map<std::uint32_t, FontFace> g_fonts;
std::uint32_t g_next_font_handle = 1;
std::string g_last_error;
TextDrawState g_ext_draw_state;

FT_Library g_freetype = nullptr;
bool g_freetype_ready = false;

constexpr std::size_t kD3D8TextureVtableLockRectIndex = 16;
constexpr std::size_t kD3D8TextureVtableUnlockRectIndex = 17;
constexpr std::size_t kD3D8DeviceVtableSetTextureIndex = 61;
constexpr std::size_t kD3D8DeviceVtableCreateStateBlockIndex = 57;
constexpr std::size_t kD3D8DeviceVtableApplyStateBlockIndex = 54;
constexpr std::size_t kD3D8DeviceVtableDeleteStateBlockIndex = 56;
constexpr std::size_t kD3D8DeviceVtableSetVertexShaderIndex = 76;
constexpr std::size_t kD3D8DeviceVtableDrawPrimitiveUPIndex = 72;

constexpr std::uint32_t kD3DPrimTriangleStrip = 5;
constexpr std::uint32_t kD3DSbtAll = 1;
constexpr std::uint32_t kD3DFvfXyzrhw = 0x00000004u;
constexpr std::uint32_t kD3DFvfDiffuse = 0x00000040u;
constexpr std::uint32_t kD3DFvfTex1 = 0x00000100u;
constexpr std::uint32_t kD3DFvfGlyphVertex = kD3DFvfXyzrhw | kD3DFvfDiffuse | kD3DFvfTex1;

std::wstring AcpToWide(const char* text);
GlyphCacheEntry* EnsureGlyphCached(FontFace* font, std::uint32_t codepoint);

void SetLastErrorString(const std::string& error) {
    g_last_error = error;
}

std::uint32_t RealToU32(double value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    if (value <= 0.0) {
        return 0;
    }
    if (value >= static_cast<double>(UINT32_MAX)) {
        return UINT32_MAX;
    }
    return static_cast<std::uint32_t>(std::llround(value));
}

int ClampByteCount(double value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    const int rounded = static_cast<int>(std::llround(value));
    if (rounded < 0) {
        return 0;
    }
    if (rounded > 4) {
        return 4;
    }
    return rounded;
}

int RealToPositiveInt(double value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    const auto rounded = static_cast<long long>(std::llround(value));
    if (rounded <= 0) {
        return 0;
    }
    if (rounded > INT_MAX) {
        return INT_MAX;
    }
    return static_cast<int>(rounded);
}

bool EnsureFreetype() {
    if (g_freetype_ready) {
        return true;
    }

    const FT_Error error = FT_Init_FreeType(&g_freetype);
    if (error != 0) {
        SetLastErrorString("FT_Init_FreeType failed");
        return false;
    }

    g_freetype_ready = true;
    return true;
}

std::wstring GetDirectoryName(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return std::wstring();
    }
    return path.substr(0, slash);
}

std::wstring GetBaseName(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::wstring JoinPath(const std::wstring& lhs, const std::wstring& rhs) {
    if (lhs.empty()) {
        return rhs;
    }
    if (rhs.empty()) {
        return lhs;
    }
    if (lhs.back() == L'\\' || lhs.back() == L'/') {
        return lhs + rhs;
    }
    return lhs + L"\\" + rhs;
}

bool FileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring GetExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (len == 0) {
        return std::wstring();
    }

    while (len >= path.size() - 1) {
        path.resize(path.size() * 2);
        len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) {
            return std::wstring();
        }
    }

    path.resize(len);
    return GetDirectoryName(path);
}

std::wstring ResolveFontPath(const char* path) {
    const std::wstring requested = AcpToWide(path);
    if (requested.empty()) {
        return std::wstring();
    }

    if (FileExists(requested)) {
        return requested;
    }

    const std::wstring basename = GetBaseName(requested);
    const std::wstring exe_dir = GetExecutableDirectory();
    if (!exe_dir.empty()) {
        const std::wstring exe_candidate = JoinPath(exe_dir, basename);
        if (FileExists(exe_candidate)) {
            return exe_candidate;
        }
    }

    wchar_t windows_dir[MAX_PATH] = {};
    const UINT windows_len = GetWindowsDirectoryW(windows_dir, MAX_PATH);
    if (windows_len > 0 && windows_len < MAX_PATH) {
        const std::wstring fonts_candidate = JoinPath(JoinPath(std::wstring(windows_dir, windows_len), L"Fonts"), basename);
        if (FileExists(fonts_candidate)) {
            return fonts_candidate;
        }
    }

    return requested;
}

std::wstring AcpToWide(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return std::wstring();
    }

    const int wide_len = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (wide_len <= 0) {
        return std::wstring();
    }

    std::wstring result(static_cast<std::size_t>(wide_len), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), wide_len) <= 0) {
        return std::wstring();
    }
    result.resize(static_cast<std::size_t>(wide_len - 1));
    return result;
}

std::wstring Utf8ToWide(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return std::wstring();
    }

    const int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (wide_len <= 0) {
        return std::wstring();
    }

    std::wstring result(static_cast<std::size_t>(wide_len), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), wide_len) <= 0) {
        return std::wstring();
    }

    result.resize(static_cast<std::size_t>(wide_len - 1));
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }

    const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) {
        return std::string();
    }

    std::string result(static_cast<std::size_t>(utf8_len), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), utf8_len, nullptr, nullptr) <= 0) {
        return std::string();
    }

    result.resize(static_cast<std::size_t>(utf8_len - 1));
    return result;
}

std::string NormalizeGmTextMarkers(const std::string& utf8) {
    std::string normalized;
    normalized.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const char ch = utf8[i];
        if (ch == '#') {
            if (!normalized.empty() && normalized.back() == '\\') {
                normalized.back() = '#';
            } else {
                normalized.push_back('\r');
            }
            continue;
        }
        normalized.push_back(ch);
    }
    return normalized;
}

bool RawStringToUtf8Auto(const char* text, std::string* out_utf8) {
    if (text == nullptr || text[0] == '\0') {
        out_utf8->clear();
        return true;
    }

    std::wstring wide = Utf8ToWide(text);
    if (wide.empty()) {
        wide = AcpToWide(text);
        if (wide.empty()) {
            SetLastErrorString("string decode failed");
            return false;
        }
    }

    *out_utf8 = WideToUtf8(wide);
    if (!wide.empty() && out_utf8->empty()) {
        SetLastErrorString("wide to utf8 conversion failed");
        return false;
    }

    *out_utf8 = NormalizeGmTextMarkers(*out_utf8);

    return true;
}

bool ReadFileBytes(const char* path, std::vector<std::uint8_t>* out_bytes) {
    const std::wstring wide_path = ResolveFontPath(path);
    if (wide_path.empty()) {
        SetLastErrorString("font path is empty or invalid");
        return false;
    }

    HANDLE file = CreateFileW(
        wide_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        SetLastErrorString("CreateFileW failed for font path");
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > INT_MAX) {
        CloseHandle(file);
        SetLastErrorString("GetFileSizeEx failed");
        return false;
    }

    out_bytes->resize(static_cast<std::size_t>(size.QuadPart));
    DWORD bytes_read = 0;
    const BOOL ok = ReadFile(
        file,
        out_bytes->data(),
        static_cast<DWORD>(out_bytes->size()),
        &bytes_read,
        nullptr);
    CloseHandle(file);

    if (!ok || bytes_read != out_bytes->size()) {
        out_bytes->clear();
        SetLastErrorString("ReadFile failed");
        return false;
    }

    return true;
}

bool DecodeNextUtf8(const std::string& bytes, std::size_t* offset, std::uint32_t* codepoint) {
    if (*offset >= bytes.size()) {
        return false;
    }

    const auto first = static_cast<unsigned char>(bytes[*offset]);
    if (first < 0x80) {
        *codepoint = first;
        *offset += 1;
        return true;
    }

    if ((first & 0xE0) == 0xC0) {
        if (*offset + 1 >= bytes.size()) {
            return false;
        }
        const auto b1 = static_cast<unsigned char>(bytes[*offset + 1]);
        if ((b1 & 0xC0) != 0x80) {
            return false;
        }
        *codepoint = ((first & 0x1F) << 6) | (b1 & 0x3F);
        *offset += 2;
        return true;
    }

    if ((first & 0xF0) == 0xE0) {
        if (*offset + 2 >= bytes.size()) {
            return false;
        }
        const auto b1 = static_cast<unsigned char>(bytes[*offset + 1]);
        const auto b2 = static_cast<unsigned char>(bytes[*offset + 2]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) {
            return false;
        }
        *codepoint = ((first & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
        *offset += 3;
        return true;
    }

    if ((first & 0xF8) == 0xF0) {
        if (*offset + 3 >= bytes.size()) {
            return false;
        }
        const auto b1 = static_cast<unsigned char>(bytes[*offset + 1]);
        const auto b2 = static_cast<unsigned char>(bytes[*offset + 2]);
        const auto b3 = static_cast<unsigned char>(bytes[*offset + 3]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
            return false;
        }
        *codepoint = ((first & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
        *offset += 4;
        return true;
    }

    return false;
}

double ClampAlpha(double value) {
    if (!std::isfinite(value)) {
        return 1.0;
    }
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

std::uint32_t PackGmColorWithAlphaByte(std::uint32_t gm_color, std::uint8_t alpha) {
    const std::uint32_t r = gm_color & 0xFFu;
    const std::uint32_t g = (gm_color >> 8) & 0xFFu;
    const std::uint32_t b = (gm_color >> 16) & 0xFFu;
    return (static_cast<std::uint32_t>(alpha) << 24) | (r << 16) | (g << 8) | b;
}

double ClampUnit(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

int RoundToInt(double value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    const long long rounded = std::llround(value);
    if (rounded < INT_MIN) {
        return INT_MIN;
    }
    if (rounded > INT_MAX) {
        return INT_MAX;
    }
    return static_cast<int>(rounded);
}

std::uint32_t LerpGmColor(std::uint32_t lhs, std::uint32_t rhs, double t) {
    t = ClampUnit(t);
    const double inv_t = 1.0 - t;
    const int lhs_r = static_cast<int>(lhs & 0xFFu);
    const int lhs_g = static_cast<int>((lhs >> 8) & 0xFFu);
    const int lhs_b = static_cast<int>((lhs >> 16) & 0xFFu);
    const int rhs_r = static_cast<int>(rhs & 0xFFu);
    const int rhs_g = static_cast<int>((rhs >> 8) & 0xFFu);
    const int rhs_b = static_cast<int>((rhs >> 16) & 0xFFu);
    const std::uint32_t out_r = static_cast<std::uint32_t>(std::lround(lhs_r * inv_t + rhs_r * t)) & 0xFFu;
    const std::uint32_t out_g = static_cast<std::uint32_t>(std::lround(lhs_g * inv_t + rhs_g * t)) & 0xFFu;
    const std::uint32_t out_b = static_cast<std::uint32_t>(std::lround(lhs_b * inv_t + rhs_b * t)) & 0xFFu;
    return out_r | (out_g << 8) | (out_b << 16);
}

uintptr_t TranslateVa(uintptr_t absolute_va) {
    const uintptr_t module_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    return module_base + (absolute_va - kPreferredImageBase);
}

int ReadRunnerInt(uintptr_t absolute_va) {
    return *reinterpret_cast<int*>(TranslateVa(absolute_va));
}

int CurrentDrawHalign() {
    return ReadRunnerInt(kVaDrawHalign);
}

int CurrentDrawValign() {
    return ReadRunnerInt(kVaDrawValign);
}

std::uint32_t NextFontHandle() {
    std::uint32_t handle = g_next_font_handle++;
    if (handle == 0) {
        handle = g_next_font_handle++;
    }
    return handle;
}

#if defined(_M_IX86)
int GMCreateEmptyTextureInternal(int width, int height, bool render_target) {
    void* fn = reinterpret_cast<void*>(TranslateVa(kVaTextureCreateEmpty));
    int result = -1;
    unsigned char flag = render_target ? 1 : 0;
    __asm {
        push esi
        mov eax, width
        mov edx, height
        mov cl, flag
        mov esi, fn
        call esi
        mov result, eax
        pop esi
    }
    return result;
}

int GMFreeTextureInternal(int texture_id) {
    void* fn = reinterpret_cast<void*>(TranslateVa(kVaTextureFree));
    int result = 0;
    __asm {
        push esi
        mov eax, texture_id
        mov esi, fn
        call esi
        mov result, eax
        pop esi
    }
    return result;
}
#else
int GMCreateEmptyTextureInternal(int, int, bool) {
    SetLastErrorString("texture creation only supported on x86");
    return -1;
}

int GMFreeTextureInternal(int) {
    SetLastErrorString("texture free only supported on x86");
    return 0;
}
#endif

GMTextureSlot* GMTextureSlots() {
    const auto slots_ptr_va = TranslateVa(kVaTextureSlotsPointer);
    const auto slots_ptr = *reinterpret_cast<uintptr_t*>(slots_ptr_va);
    if (slots_ptr == 0) {
        return nullptr;
    }
    return reinterpret_cast<GMTextureSlot*>(slots_ptr);
}

GMTextureSlot* LookupTextureSlot(int texture_id) {
    if (texture_id < 0) {
        return nullptr;
    }

    GMTextureSlot* slots = GMTextureSlots();
    if (slots == nullptr) {
        SetLastErrorString("texture slot table is null");
        return nullptr;
    }
    GMTextureSlot* slot = &slots[texture_id];
    if (!slot->valid || slot->texture == nullptr) {
        return nullptr;
    }

    return slot;
}

void* GMD3DDevice() {
    const auto device_holder_va = TranslateVa(kVaD3DDevicePointer);
    const auto device_holder = *reinterpret_cast<void**>(device_holder_va);
    if (device_holder == nullptr) {
        return nullptr;
    }

    const auto device = *reinterpret_cast<void**>(device_holder);
    return device;
}

HRESULT TextureLockRect(void* texture, UINT level, D3DLOCKED_RECT* locked_rect, const RECT* rect, DWORD flags) {
    if (texture == nullptr) {
        return E_POINTER;
    }

    auto** vtable = *reinterpret_cast<void***>(texture);
    using Fn = HRESULT(__stdcall*)(void*, UINT, D3DLOCKED_RECT*, const RECT*, DWORD);
    auto fn = reinterpret_cast<Fn>(vtable[kD3D8TextureVtableLockRectIndex]);
    return fn(texture, level, locked_rect, rect, flags);
}

HRESULT TextureUnlockRect(void* texture, UINT level) {
    if (texture == nullptr) {
        return E_POINTER;
    }

    auto** vtable = *reinterpret_cast<void***>(texture);
    using Fn = HRESULT(__stdcall*)(void*, UINT);
    auto fn = reinterpret_cast<Fn>(vtable[kD3D8TextureVtableUnlockRectIndex]);
    return fn(texture, level);
}

HRESULT DeviceSetTexture(void* device, DWORD stage, void* texture) {
    if (device == nullptr) {
        return E_POINTER;
    }

    auto** vtable = *reinterpret_cast<void***>(device);
    using Fn = HRESULT(__stdcall*)(void*, DWORD, void*);
    auto fn = reinterpret_cast<Fn>(vtable[kD3D8DeviceVtableSetTextureIndex]);
    return fn(device, stage, texture);
}

HRESULT DeviceCreateStateBlock(void* device, DWORD type, DWORD* token) {
    if (device == nullptr) {
        return E_POINTER;
    }

    auto** vtable = *reinterpret_cast<void***>(device);
    using Fn = HRESULT(__stdcall*)(void*, DWORD, DWORD*);
    auto fn = reinterpret_cast<Fn>(vtable[kD3D8DeviceVtableCreateStateBlockIndex]);
    return fn(device, type, token);
}

HRESULT DeviceApplyStateBlock(void* device, DWORD token) {
    if (device == nullptr) {
        return E_POINTER;
    }

    auto** vtable = *reinterpret_cast<void***>(device);
    using Fn = HRESULT(__stdcall*)(void*, DWORD);
    auto fn = reinterpret_cast<Fn>(vtable[kD3D8DeviceVtableApplyStateBlockIndex]);
    return fn(device, token);
}

HRESULT DeviceDeleteStateBlock(void* device, DWORD token) {
    if (device == nullptr) {
        return E_POINTER;
    }

    auto** vtable = *reinterpret_cast<void***>(device);
    using Fn = HRESULT(__stdcall*)(void*, DWORD);
    auto fn = reinterpret_cast<Fn>(vtable[kD3D8DeviceVtableDeleteStateBlockIndex]);
    return fn(device, token);
}

HRESULT DeviceSetVertexShader(void* device, DWORD shader) {
    if (device == nullptr) {
        return E_POINTER;
    }

    auto** vtable = *reinterpret_cast<void***>(device);
    using Fn = HRESULT(__stdcall*)(void*, DWORD);
    auto fn = reinterpret_cast<Fn>(vtable[kD3D8DeviceVtableSetVertexShaderIndex]);
    return fn(device, shader);
}

HRESULT DeviceDrawPrimitiveUP(void* device, UINT primitive_type, UINT primitive_count, const void* data, UINT stride) {
    if (device == nullptr) {
        return E_POINTER;
    }

    auto** vtable = *reinterpret_cast<void***>(device);
    using Fn = HRESULT(__stdcall*)(void*, UINT, UINT, const void*, UINT);
    auto fn = reinterpret_cast<Fn>(vtable[kD3D8DeviceVtableDrawPrimitiveUPIndex]);
    return fn(device, primitive_type, primitive_count, data, stride);
}

bool UploadArgbPixelsToTexture(int texture_id, int width, int height, const std::vector<std::uint32_t>& pixels) {
    GMTextureSlot* slot = LookupTextureSlot(texture_id);
    if (slot == nullptr) {
        SetLastErrorString("texture slot is invalid");
        return false;
    }

    if (width <= 0 || height <= 0) {
        SetLastErrorString("texture upload dimensions are invalid");
        return false;
    }

    D3DLOCKED_RECT locked = {};
    if (FAILED(TextureLockRect(slot->texture, 0, &locked, nullptr, 0))) {
        SetLastErrorString("texture LockRect failed");
        return false;
    }

    std::memset(locked.pBits, 0, static_cast<std::size_t>(locked.Pitch) * static_cast<std::size_t>(slot->texture_height));
    for (int y = 0; y < height; ++y) {
        auto* dst = reinterpret_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch);
        const auto* src = reinterpret_cast<const std::uint8_t*>(pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width));
        std::memcpy(dst, src, static_cast<std::size_t>(width) * sizeof(std::uint32_t));
    }

    if (FAILED(TextureUnlockRect(slot->texture, 0))) {
        SetLastErrorString("texture UnlockRect failed");
        return false;
    }
    return true;
}

double MeasureUtf8String(FT_Face face, const std::string& bytes) {
    double advance = 0.0;
    double max_width = 0.0;
    FT_UInt previous = 0;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        std::uint32_t codepoint = 0;
        if (!DecodeNextUtf8(bytes, &offset, &codepoint)) {
            SetLastErrorString("invalid utf-8 sequence");
            return -1.0;
        }

        if (codepoint == '\r') {
            continue;
        }
        if (codepoint == '\n') {
            if (advance > max_width) {
                max_width = advance;
            }
            advance = 0.0;
            previous = 0;
            continue;
        }

        const FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
        if (FT_HAS_KERNING(face) && previous != 0 && glyph_index != 0) {
            FT_Vector delta = {};
            if (FT_Get_Kerning(face, previous, glyph_index, FT_KERNING_DEFAULT, &delta) == 0) {
                advance += static_cast<double>(delta.x) / 64.0;
            }
        }

        if (FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT) != 0) {
            SetLastErrorString("FT_Load_Glyph failed");
            return -1.0;
        }

        advance += static_cast<double>(face->glyph->advance.x) / 64.0;
        previous = glyph_index;
    }

    if (advance > max_width) {
        max_width = advance;
    }
    return max_width;
}

std::string NormalizeNewlines(const std::string& utf8) {
    std::string normalized;
    normalized.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const char ch = utf8[i];
        if (ch == '\r') {
            normalized.push_back('\n');
            if (i + 1 < utf8.size() && utf8[i + 1] == '\n') {
                ++i;
            }
            continue;
        }
        normalized.push_back(ch);
    }
    return normalized;
}

std::vector<std::string> SplitNormalizedLines(const std::string& utf8) {
    std::vector<std::string> lines;
    std::string current;
    for (char ch : NormalizeNewlines(utf8)) {
        if (ch == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    lines.push_back(current);
    return lines;
}

std::vector<std::string> SplitWrapTokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::size_t token_start = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == ' ' || ch == '-') {
            tokens.push_back(line.substr(token_start, i - token_start + 1));
            token_start = i + 1;
        }
    }

    if (token_start < line.size()) {
        tokens.push_back(line.substr(token_start));
    } else if (tokens.empty() && !line.empty()) {
        tokens.push_back(line);
    }

    return tokens;
}

bool WrapLogicalLine(FT_Face face, const std::string& line, double wrap_width, std::vector<std::string>* out_lines) {
    if (wrap_width < 0.0 || line.empty()) {
        out_lines->push_back(line);
        return true;
    }

    const std::vector<std::string> tokens = SplitWrapTokens(line);
    if (tokens.empty()) {
        out_lines->push_back(line);
        return true;
    }

    std::string current = tokens.front();
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string combined = current + tokens[i];
        const double combined_width = MeasureUtf8String(face, combined);
        if (combined_width < 0.0) {
            return false;
        }

        if (combined_width <= wrap_width) {
            current = combined;
            continue;
        }

        out_lines->push_back(current);
        current = tokens[i];
    }

    out_lines->push_back(current);
    return true;
}

bool CalculateTextLayout(FT_Face face, const std::string& bytes, TextLayout* layout) {
    const int padding = 1;
    const int ascender = std::max(1, static_cast<int>(std::ceil(static_cast<double>(face->size->metrics.ascender) / 64.0)));
    const int descender = std::max(0, static_cast<int>(std::ceil(static_cast<double>(-face->size->metrics.descender) / 64.0)));
    const int line_height = std::max(1, static_cast<int>(std::ceil(static_cast<double>(face->size->metrics.height) / 64.0)));

    double min_x = 0.0;
    double max_x = 0.0;
    double pen_x = 0.0;
    int line_count = 1;
    FT_UInt previous = 0;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        std::uint32_t codepoint = 0;
        if (!DecodeNextUtf8(bytes, &offset, &codepoint)) {
            SetLastErrorString("invalid utf-8 sequence");
            return false;
        }

        if (codepoint == '\r') {
            continue;
        }
        if (codepoint == '\n') {
            if (pen_x > max_x) {
                max_x = pen_x;
            }
            pen_x = 0.0;
            previous = 0;
            ++line_count;
            continue;
        }

        const FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
        if (FT_HAS_KERNING(face) && previous != 0 && glyph_index != 0) {
            FT_Vector delta = {};
            if (FT_Get_Kerning(face, previous, glyph_index, FT_KERNING_DEFAULT, &delta) == 0) {
                pen_x += static_cast<double>(delta.x) / 64.0;
            }
        }

        if (FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT) != 0) {
            SetLastErrorString("FT_Load_Glyph failed");
            return false;
        }

        const double left = pen_x + static_cast<double>(face->glyph->metrics.horiBearingX) / 64.0;
        const double right = left + static_cast<double>(face->glyph->metrics.width) / 64.0;
        if (left < min_x) {
            min_x = left;
        }
        if (right > max_x) {
            max_x = right;
        }

        pen_x += static_cast<double>(face->glyph->advance.x) / 64.0;
        previous = glyph_index;
    }

    if (pen_x > max_x) {
        max_x = pen_x;
    }

    layout->origin_x = static_cast<double>(padding) - min_x;
    layout->width = std::max(1, static_cast<int>(std::ceil(max_x - min_x)) + padding * 2);
    layout->line_height = std::max(line_height, ascender + descender);
    layout->height = std::max(1, layout->line_height * line_count + padding * 2);
    layout->baseline = padding + ascender;
    return true;
}

bool ShapePreparedLine(FontFace* font, const std::string& utf8, PreparedTextLine* out_line) {
    if (font == nullptr || font->face == nullptr || out_line == nullptr) {
        SetLastErrorString("shape_line: invalid font");
        return false;
    }

    out_line->utf8 = utf8;
    out_line->glyphs.clear();
    out_line->advance_width = 0.0;
    if (!CalculateTextLayout(font->face, utf8, &out_line->layout)) {
        return false;
    }

    double pen_x = 0.0;
    FT_UInt previous = 0;
    std::size_t offset = 0;
    while (offset < utf8.size()) {
        std::uint32_t codepoint = 0;
        if (!DecodeNextUtf8(utf8, &offset, &codepoint)) {
            SetLastErrorString("invalid utf-8 sequence");
            return false;
        }

        if (codepoint == '\r' || codepoint == '\n') {
            continue;
        }

        const FT_UInt glyph_index = FT_Get_Char_Index(font->face, codepoint);
        if (FT_HAS_KERNING(font->face) && previous != 0 && glyph_index != 0) {
            FT_Vector delta = {};
            if (FT_Get_Kerning(font->face, previous, glyph_index, FT_KERNING_DEFAULT, &delta) == 0) {
                pen_x += static_cast<double>(delta.x) / 64.0;
            }
        }

        GlyphCacheEntry* glyph = EnsureGlyphCached(font, codepoint);
        if (glyph == nullptr) {
            return false;
        }

        ShapedGlyph shaped;
        shaped.texture_id = glyph->texture_id;
        shaped.width = glyph->width;
        shaped.height = glyph->height;
        shaped.pen_x = pen_x;
        shaped.advance_x = glyph->advance_x;
        shaped.local_x = out_line->layout.origin_x + pen_x + static_cast<double>(glyph->bitmap_left);
        shaped.local_y = static_cast<double>(out_line->layout.baseline - glyph->bitmap_top);

        if (glyph->texture_id >= 0) {
            GMTextureSlot* slot = LookupTextureSlot(glyph->texture_id);
            if (slot == nullptr) {
                SetLastErrorString("shape_line: glyph texture slot is invalid");
                return false;
            }
            shaped.texture_width = slot->texture_width;
            shaped.texture_height = slot->texture_height;
        }

        out_line->glyphs.push_back(shaped);
        pen_x += glyph->advance_x;
        previous = glyph_index;
    }

    out_line->advance_width = pen_x;
    return true;
}

bool PrepareWrappedText(FontFace* font, const std::string& utf8, double wrap_width, std::vector<PreparedTextLine>* out_lines) {
    out_lines->clear();
    for (const std::string& logical_line : SplitNormalizedLines(utf8)) {
        std::vector<std::string> wrapped;
        if (!WrapLogicalLine(font->face, logical_line, wrap_width, &wrapped)) {
            return false;
        }
        for (const std::string& line : wrapped) {
            PreparedTextLine prepared;
            if (!ShapePreparedLine(font, line, &prepared)) {
                return false;
            }
            out_lines->push_back(std::move(prepared));
        }
    }

    if (out_lines->empty()) {
        PreparedTextLine prepared;
        if (!ShapePreparedLine(font, std::string(), &prepared)) {
            return false;
        }
        out_lines->push_back(std::move(prepared));
    }

    return true;
}

std::uint32_t BlendPixel(std::uint32_t dst, std::uint8_t coverage) {
    if (coverage == 0) {
        return dst;
    }

    const int src_a = coverage;
    const int src_r = 255;
    const int src_g = 255;
    const int src_b = 255;

    const int dst_a = (dst >> 24) & 0xFF;
    const int dst_r = (dst >> 16) & 0xFF;
    const int dst_g = (dst >> 8) & 0xFF;
    const int dst_b = dst & 0xFF;

    const int out_a = src_a + ((dst_a * (255 - src_a)) / 255);
    if (out_a <= 0) {
        return 0;
    }

    const int src_pr = src_r * src_a;
    const int src_pg = src_g * src_a;
    const int src_pb = src_b * src_a;
    const int dst_pr = dst_r * dst_a;
    const int dst_pg = dst_g * dst_a;
    const int dst_pb = dst_b * dst_a;

    const int out_pr = src_pr + ((dst_pr * (255 - src_a)) / 255);
    const int out_pg = src_pg + ((dst_pg * (255 - src_a)) / 255);
    const int out_pb = src_pb + ((dst_pb * (255 - src_a)) / 255);

    const int out_r = out_pr / out_a;
    const int out_g = out_pg / out_a;
    const int out_b = out_pb / out_a;

    return (static_cast<std::uint32_t>(out_a) << 24) |
           (static_cast<std::uint32_t>(out_r) << 16) |
           (static_cast<std::uint32_t>(out_g) << 8) |
           static_cast<std::uint32_t>(out_b);
}

void BlitGlyphBitmap(const FT_Bitmap& bitmap, int dst_x, int dst_y, int width, int height, std::vector<std::uint32_t>* pixels) {
    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.pixel_mode != FT_PIXEL_MODE_MONO) {
        return;
    }

    for (int row = 0; row < static_cast<int>(bitmap.rows); ++row) {
        const int y = dst_y + row;
        if (y < 0 || y >= height) {
            continue;
        }

        for (int col = 0; col < static_cast<int>(bitmap.width); ++col) {
            const int x = dst_x + col;
            if (x < 0 || x >= width) {
                continue;
            }

            std::uint8_t coverage = 0;
            if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
                coverage = bitmap.buffer[static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.pitch) + static_cast<std::size_t>(col)];
            } else {
                const auto bits = bitmap.buffer[static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.pitch) + static_cast<std::size_t>(col / 8)];
                coverage = ((bits >> (7 - (col % 8))) & 1) != 0 ? 255 : 0;
            }

            std::uint32_t& dst = (*pixels)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
            dst = BlendPixel(dst, coverage);
        }
    }
}

void FreeGlyphCache(FontFace* font) {
    if (font == nullptr) {
        return;
    }

    for (auto& [_, glyph] : font->glyph_cache) {
        if (glyph.texture_id >= 0) {
            GMFreeTextureInternal(glyph.texture_id);
        }
    }
    font->glyph_cache.clear();
}

bool RenderGlyphPixels(const FT_Bitmap& bitmap, int width, int height, std::vector<std::uint32_t>* pixels) {
    pixels->assign(static_cast<std::size_t>(std::max(width, 1)) * static_cast<std::size_t>(std::max(height, 1)), 0);
    if (width <= 0 || height <= 0) {
        return true;
    }

    BlitGlyphBitmap(bitmap, 0, 0, width, height, pixels);
    return true;
}

GlyphCacheEntry* EnsureGlyphCached(FontFace* font, std::uint32_t codepoint) {
    if (font == nullptr || font->face == nullptr) {
        SetLastErrorString("glyph cache: invalid font");
        return nullptr;
    }

    auto it = font->glyph_cache.find(codepoint);
    if (it != font->glyph_cache.end()) {
        return &it->second;
    }

    const FT_UInt glyph_index = FT_Get_Char_Index(font->face, codepoint);
    if (FT_Load_Glyph(font->face, glyph_index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
        SetLastErrorString("FT_Load_Glyph render failed");
        return nullptr;
    }

    const FT_GlyphSlot glyph = font->face->glyph;
    GlyphCacheEntry entry;
    entry.width = static_cast<int>(glyph->bitmap.width);
    entry.height = static_cast<int>(glyph->bitmap.rows);
    entry.bitmap_left = glyph->bitmap_left;
    entry.bitmap_top = glyph->bitmap_top;
    entry.advance_x = static_cast<double>(glyph->advance.x) / 64.0;
    entry.has_bitmap = entry.width > 0 && entry.height > 0;

    if (entry.has_bitmap) {
        std::vector<std::uint32_t> pixels;
        RenderGlyphPixels(glyph->bitmap, entry.width, entry.height, &pixels);

        const int texture_id = GMCreateEmptyTextureInternal(entry.width, entry.height, false);
        if (texture_id < 0) {
            SetLastErrorString("glyph cache: GM create empty texture failed");
            return nullptr;
        }

        if (!UploadArgbPixelsToTexture(texture_id, entry.width, entry.height, pixels)) {
            GMFreeTextureInternal(texture_id);
            return nullptr;
        }

        entry.texture_id = texture_id;
    }

    auto [inserted_it, _] = font->glyph_cache.emplace(codepoint, entry);
    return &inserted_it->second;
}

bool DrawTransformedGlyphQuad(
    void* device,
    const ShapedGlyph& glyph,
    double origin_x,
    double origin_y,
    double cos_angle,
    double sin_angle,
    double xscale,
    double yscale,
    std::uint32_t color_tl,
    std::uint32_t color_tr,
    std::uint32_t color_br,
    std::uint32_t color_bl) {
    if (glyph.texture_id < 0 || glyph.width <= 0 || glyph.height <= 0) {
        return true;
    }

    GMTextureSlot* slot = LookupTextureSlot(glyph.texture_id);
    if (slot == nullptr) {
        SetLastErrorString("glyph draw: texture slot is invalid");
        return false;
    }

    const double x0_local = glyph.local_x;
    const double y0_local = glyph.local_y;
    const double x1_local = x0_local + static_cast<double>(glyph.width);
    const double y1_local = y0_local + static_cast<double>(glyph.height);

    const auto transform_x = [&](double local_x, double local_y) {
        return static_cast<float>(origin_x + cos_angle * (local_x * xscale) + sin_angle * (local_y * yscale) - 0.5);
    };
    const auto transform_y = [&](double local_x, double local_y) {
        return static_cast<float>(origin_y - sin_angle * (local_x * xscale) + cos_angle * (local_y * yscale) - 0.5);
    };

    const float u1 = glyph.texture_width > 0 ? static_cast<float>(glyph.width) / static_cast<float>(glyph.texture_width) : 1.0f;
    const float v1 = glyph.texture_height > 0 ? static_cast<float>(glyph.height) / static_cast<float>(glyph.texture_height) : 1.0f;

    GMDrawVertex vertices[4] = {
        {transform_x(x0_local, y0_local), transform_y(x0_local, y0_local), 0.0f, 1.0f, color_tl, 0.0f, 0.0f},
        {transform_x(x1_local, y0_local), transform_y(x1_local, y0_local), 0.0f, 1.0f, color_tr, u1, 0.0f},
        {transform_x(x0_local, y1_local), transform_y(x0_local, y1_local), 0.0f, 1.0f, color_bl, 0.0f, v1},
        {transform_x(x1_local, y1_local), transform_y(x1_local, y1_local), 0.0f, 1.0f, color_br, u1, v1},
    };

    bool ok = true;
    ok = ok && SUCCEEDED(DeviceSetTexture(device, 0, slot->texture));
    ok = ok && SUCCEEDED(DeviceDrawPrimitiveUP(device, kD3DPrimTriangleStrip, 2, vertices, sizeof(GMDrawVertex)));
    if (!ok && g_last_error.empty()) {
        SetLastErrorString("glyph draw: D3D draw failed");
    }

    return ok;
}

bool DrawTextCore(FontFace* font, const std::string& utf8, double x, double y, const TextDrawState& state) {
    if (font == nullptr || font->face == nullptr) {
        SetLastErrorString("draw_text: invalid font");
        return false;
    }

    std::vector<PreparedTextLine> lines;
    if (!PrepareWrappedText(font, utf8, state.wrap_width, &lines)) {
        return false;
    }

    void* device = GMD3DDevice();
    if (device == nullptr) {
        SetLastErrorString("draw_text: D3D device is null");
        return false;
    }

    if (FAILED(DeviceSetVertexShader(device, kD3DFvfGlyphVertex))) {
        SetLastErrorString("draw_text: failed to set vertex shader");
        return false;
    }

    const double line_sep = state.line_sep < 0.0
        ? std::max(1.0, static_cast<double>(font->face->size->metrics.height) / 64.0)
        : state.line_sep;
    const double angle_radians = state.angle_degrees * 3.14159265358979323846 / 180.0;
    const double sin_angle = std::sin(angle_radians);
    const double cos_angle = std::cos(angle_radians);
    const double line_step_x = sin_angle * state.yscale * line_sep;
    const double line_step_y = cos_angle * state.yscale * line_sep;

    double origin_x = x;
    double origin_y = y;
    const double line_count = static_cast<double>(lines.size());
    const int valign = CurrentDrawValign();
    if (valign == 1) {
        origin_x -= static_cast<double>(RoundToInt(line_count * line_step_x / 2.0));
        origin_y -= static_cast<double>(RoundToInt(line_count * line_step_y / 2.0));
    } else if (valign == 2) {
        origin_x -= static_cast<double>(RoundToInt(line_count * line_step_x));
        origin_y -= static_cast<double>(RoundToInt(line_count * line_step_y));
    }

    const std::uint8_t alpha_byte = static_cast<std::uint8_t>(std::lround(ClampAlpha(state.alpha) * 255.0));
    const int halign = CurrentDrawHalign();
    bool ok = true;

    for (std::size_t line_index = 0; ok && line_index < lines.size(); ++line_index) {
        const PreparedTextLine& line = lines[line_index];
        double line_origin_x = origin_x + static_cast<double>(line_index) * line_step_x;
        double line_origin_y = origin_y + static_cast<double>(line_index) * line_step_y;

        double align_dx = 0.0;
        if (halign == 1) {
            align_dx = -static_cast<double>(RoundToInt(line.advance_width * state.xscale / 2.0));
        } else if (halign == 2) {
            align_dx = -static_cast<double>(RoundToInt(line.advance_width * state.xscale));
        }
        line_origin_x += cos_angle * align_dx;
        line_origin_y -= sin_angle * align_dx;

        const double width_for_gradient = line.advance_width > 0.0 ? line.advance_width : 1.0;
        for (const ShapedGlyph& glyph : line.glyphs) {
            if (glyph.texture_id < 0) {
                continue;
            }

            std::uint32_t color_tl = PackGmColorWithAlphaByte(state.c1, alpha_byte);
            std::uint32_t color_tr = color_tl;
            std::uint32_t color_br = color_tl;
            std::uint32_t color_bl = color_tl;

            if (state.use_gradient) {
                const double t0 = ClampUnit(glyph.pen_x / width_for_gradient);
                const double t1 = ClampUnit((glyph.pen_x + glyph.advance_x) / width_for_gradient);
                color_tl = PackGmColorWithAlphaByte(LerpGmColor(state.c1, state.c2, t0), alpha_byte);
                color_tr = PackGmColorWithAlphaByte(LerpGmColor(state.c1, state.c2, t1), alpha_byte);
                color_br = PackGmColorWithAlphaByte(LerpGmColor(state.c4, state.c3, t1), alpha_byte);
                color_bl = PackGmColorWithAlphaByte(LerpGmColor(state.c4, state.c3, t0), alpha_byte);
            }

            ok = DrawTransformedGlyphQuad(
                device,
                glyph,
                line_origin_x,
                line_origin_y,
                cos_angle,
                sin_angle,
                state.xscale,
                state.yscale,
                color_tl,
                color_tr,
                color_br,
                color_bl);
            if (!ok) {
                break;
            }
        }
    }

    DeviceSetTexture(device, 0, nullptr);
    if (ok) {
        SetLastErrorString("");
    }
    return ok;
}

double DrawTextExtTransformedColorFromRawString(FontFace* font, const char* text, double x, double y) {
    std::string utf8;
    if (!RawStringToUtf8Auto(text, &utf8)) {
        return 0.0;
    }

    if (!DrawTextCore(font, utf8, x, y, g_ext_draw_state)) {
        return 0.0;
    }

    return 1.0;
}

double DefaultLineSep(FontFace* font) {
    if (font == nullptr || font->face == nullptr) {
        return 1.0;
    }
    return std::max(1.0, static_cast<double>(font->face->size->metrics.height) / 64.0);
}

bool MeasureWrappedText(FontFace* font, const std::string& utf8, double wrap_width, double line_sep, double* out_width, double* out_height) {
    if (font == nullptr || font->face == nullptr) {
        SetLastErrorString("measure_text: invalid font");
        return false;
    }

    std::vector<PreparedTextLine> lines;
    if (!PrepareWrappedText(font, utf8, wrap_width, &lines)) {
        return false;
    }

    double max_width = 0.0;
    for (const PreparedTextLine& line : lines) {
        if (line.advance_width > max_width) {
            max_width = line.advance_width;
        }
    }

    const double effective_sep = line_sep < 0.0 ? DefaultLineSep(font) : line_sep;
    if (out_width != nullptr) {
        *out_width = max_width;
    }
    if (out_height != nullptr) {
        *out_height = std::max(1.0, effective_sep * static_cast<double>(std::max<std::size_t>(lines.size(), 1)));
    }
    SetLastErrorString("");
    return true;
}

double MeasureWidthFromRawString(FontFace* font, const char* text, double wrap_width) {
    std::string utf8;
    if (!RawStringToUtf8Auto(text, &utf8)) {
        return -1.0;
    }

    double width = -1.0;
    if (!MeasureWrappedText(font, utf8, wrap_width, -1.0, &width, nullptr)) {
        return -1.0;
    }
    return width;
}

double MeasureHeightFromRawString(FontFace* font, const char* text, double line_sep, double wrap_width) {
    std::string utf8;
    if (!RawStringToUtf8Auto(text, &utf8)) {
        return -1.0;
    }

    double height = -1.0;
    if (!MeasureWrappedText(font, utf8, wrap_width, line_sep, nullptr, &height)) {
        return -1.0;
    }
    return height;
}

}  // namespace

extern "C" {

__declspec(dllexport) const char* __cdecl gm82font_last_error() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_last_error.c_str();
}

__declspec(dllexport) double __cdecl gm82font_font_add(const char* path, double pixel_size_value) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!EnsureFreetype()) {
        return -1.0;
    }

    const int pixel_size = RealToPositiveInt(pixel_size_value);
    if (pixel_size <= 0) {
        SetLastErrorString("font_add: pixel size must be positive");
        return -1.0;
    }

    std::vector<std::uint8_t> file_data;
    if (!ReadFileBytes(path, &file_data)) {
        return -1.0;
    }

    FT_Face face = nullptr;
    const FT_Error new_face_error = FT_New_Memory_Face(
        g_freetype,
        reinterpret_cast<const FT_Byte*>(file_data.data()),
        static_cast<FT_Long>(file_data.size()),
        0,
        &face);
    if (new_face_error != 0 || face == nullptr) {
        SetLastErrorString("FT_New_Memory_Face failed");
        return -1.0;
    }

    const FT_Error size_error = FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixel_size));
    if (size_error != 0) {
        FT_Done_Face(face);
        SetLastErrorString("FT_Set_Pixel_Sizes failed");
        return -1.0;
    }

    const std::uint32_t handle = NextFontHandle();
    FontFace& font = g_fonts[handle];
    font.file_data = std::move(file_data);
    font.face = face;

    SetLastErrorString("");
    return static_cast<double>(handle);
}

__declspec(dllexport) double __cdecl gm82font_font_delete(double handle_value) {
    const std::uint32_t handle = RealToU32(handle_value);
    std::lock_guard<std::mutex> lock(g_mutex);

    auto it = g_fonts.find(handle);
    if (handle == 0 || it == g_fonts.end()) {
        SetLastErrorString("font_delete: invalid handle");
        return 0.0;
    }

    FreeGlyphCache(&it->second);
    if (it->second.face != nullptr) {
        FT_Done_Face(it->second.face);
    }
    g_fonts.erase(it);
    SetLastErrorString("");
    return 1.0;
}

__declspec(dllexport) double __cdecl gm82font_string_width(double font_handle_value, const char* text) {
    std::lock_guard<std::mutex> lock(g_mutex);

    const std::uint32_t font_handle = RealToU32(font_handle_value);
    auto font_it = g_fonts.find(font_handle);
    if (font_handle == 0 || font_it == g_fonts.end()) {
        SetLastErrorString("string_width: invalid font handle");
        return -1.0;
    }

    return MeasureWidthFromRawString(&font_it->second, text, -1.0);
}

__declspec(dllexport) double __cdecl gm82font_string_width_ext(
    double font_handle_value,
    const char* text,
    double sep_value,
    double wrap_width_value) {
    (void)sep_value;
    std::lock_guard<std::mutex> lock(g_mutex);

    const std::uint32_t font_handle = RealToU32(font_handle_value);
    auto font_it = g_fonts.find(font_handle);
    if (font_handle == 0 || font_it == g_fonts.end()) {
        SetLastErrorString("string_width_ext: invalid font handle");
        return -1.0;
    }

    return MeasureWidthFromRawString(&font_it->second, text, wrap_width_value);
}

__declspec(dllexport) double __cdecl gm82font_string_height(double font_handle_value, const char* text) {
    std::lock_guard<std::mutex> lock(g_mutex);

    const std::uint32_t font_handle = RealToU32(font_handle_value);
    auto font_it = g_fonts.find(font_handle);
    if (font_handle == 0 || font_it == g_fonts.end()) {
        SetLastErrorString("string_height: invalid font handle");
        return -1.0;
    }

    return MeasureHeightFromRawString(&font_it->second, text, -1.0, -1.0);
}

__declspec(dllexport) double __cdecl gm82font_string_height_ext(
    double font_handle_value,
    const char* text,
    double sep_value,
    double wrap_width_value) {
    std::lock_guard<std::mutex> lock(g_mutex);

    const std::uint32_t font_handle = RealToU32(font_handle_value);
    auto font_it = g_fonts.find(font_handle);
    if (font_handle == 0 || font_it == g_fonts.end()) {
        SetLastErrorString("string_height_ext: invalid font handle");
        return -1.0;
    }

    return MeasureHeightFromRawString(&font_it->second, text, sep_value, wrap_width_value);
}

__declspec(dllexport) double __cdecl gm82font_set_text_transform(
    double sep_value,
    double wrap_width_value,
    double xscale_value,
    double yscale_value,
    double angle_value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ext_draw_state.line_sep = std::isfinite(sep_value) ? sep_value : -1.0;
    g_ext_draw_state.wrap_width = std::isfinite(wrap_width_value) ? wrap_width_value : -1.0;
    g_ext_draw_state.xscale = std::isfinite(xscale_value) ? xscale_value : 1.0;
    g_ext_draw_state.yscale = std::isfinite(yscale_value) ? yscale_value : 1.0;
    g_ext_draw_state.angle_degrees = std::isfinite(angle_value) ? angle_value : 0.0;
    SetLastErrorString("");
    return 1.0;
}

__declspec(dllexport) double __cdecl gm82font_set_text_gradient(
    double c1_value,
    double c2_value,
    double c3_value,
    double c4_value,
    double alpha_value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ext_draw_state.c1 = std::isfinite(c1_value) ? (static_cast<std::uint32_t>(std::llround(c1_value)) & 0x00FFFFFFu) : 0x00FFFFFFu;
    g_ext_draw_state.c2 = std::isfinite(c2_value) ? (static_cast<std::uint32_t>(std::llround(c2_value)) & 0x00FFFFFFu) : 0x00FFFFFFu;
    g_ext_draw_state.c3 = std::isfinite(c3_value) ? (static_cast<std::uint32_t>(std::llround(c3_value)) & 0x00FFFFFFu) : 0x00FFFFFFu;
    g_ext_draw_state.c4 = std::isfinite(c4_value) ? (static_cast<std::uint32_t>(std::llround(c4_value)) & 0x00FFFFFFu) : 0x00FFFFFFu;
    g_ext_draw_state.alpha = ClampAlpha(alpha_value);
    g_ext_draw_state.use_gradient = true;
    SetLastErrorString("");
    return 1.0;
}

__declspec(dllexport) double __cdecl gm82font_draw_text_ext_transformed_color(
    double font_handle_value,
    double x_value,
    double y_value,
    const char* text) {
    std::lock_guard<std::mutex> lock(g_mutex);

    const std::uint32_t font_handle = RealToU32(font_handle_value);
    auto font_it = g_fonts.find(font_handle);
    if (font_handle == 0 || font_it == g_fonts.end()) {
        SetLastErrorString("draw_text_ext_transformed_color: invalid font handle");
        return 0.0;
    }

    return DrawTextExtTransformedColorFromRawString(&font_it->second, text, x_value, y_value);
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& [_, font] : g_fonts) {
            FreeGlyphCache(&font);
            if (font.face != nullptr) {
                FT_Done_Face(font.face);
            }
        }
        g_fonts.clear();
        if (g_freetype_ready && g_freetype != nullptr) {
            FT_Done_FreeType(g_freetype);
            g_freetype = nullptr;
            g_freetype_ready = false;
        }
    }
    return TRUE;
}

}  // extern "C"
