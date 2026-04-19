#define Create_0
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
//create
gm82font_init();

font_path = "simhei.ttf";
font_size = 24;
f = gm82font_font_add(font_path, font_size);
angle = 0;
sample_basic = "draw_text:#中文ABC#第二行#\\#号保留";
sample_wrap = "中文 ABC DEF GHI JKL # 第二段测试";
sample_color = "渐变颜色测试";
sample_transform = "旋转缩放测试";
sample_ext_transform = "中文 mixed wrap demo # 第二行看看";
measure_w = 0;
measure_h = 0;
measure_w_ext = 0;
measure_h_ext = 0;
status = "ready";
#define Step_0
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
//step
angle += 1;
if (angle >= 360) {
    angle -= 360;
}

if (keyboard_check_pressed(ord("D"))) {
    if (f >= 0) {
        gm82font_font_delete(f);
        f = -1;
        status = "font deleted";
    }
}

if (keyboard_check_pressed(ord("A"))) {
    if (f < 0) {
        f = gm82font_font_add(font_path, font_size);
        status = "font added";
    } else {
        status = "font already alive";
    }
}

if (keyboard_check_pressed(ord("R"))) {
    if (f >= 0) {
        gm82font_font_delete(f);
    }
    f = gm82font_font_add(font_path, font_size);
    status = "font recreated";
}

if (f >= 0) {
    measure_w = gm82font_string_width(sample_basic);
    measure_h = gm82font_string_height(sample_basic);
    measure_w_ext = gm82font_string_width_ext(sample_wrap, 28, 180);
    measure_h_ext = gm82font_string_height_ext(sample_wrap, 28, 180);
}
#define Draw_0
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
//draw
var bx;
var by;

draw_clear(make_color_rgb(18, 22, 28));
draw_set_color(c_white);
draw_set_alpha(1);
draw_set_halign(fa_left);
draw_set_valign(fa_top);

draw_text(16, 16, "A add  D delete  R recreate");
draw_text(16, 32, "font=" + string(f) + "  status=" + status);
draw_text(16, 48, "string_width=" + string(measure_w) + "  string_height=" + string(measure_h));
draw_text(16, 64, "string_width_ext=" + string(measure_w_ext) + "  string_height_ext=" + string(measure_h_ext));

if (f < 0) {
    draw_set_color(c_red);
    draw_text(16, 96, "font handle invalid");
    exit;
}

gm82font_draw_set_font(f);

draw_set_color(c_white);
draw_set_alpha(1);
gm82font_draw_text(16, 100, sample_basic);

bx = 16;
by = 180;
draw_set_color(make_color_rgb(60, 90, 120));
draw_rectangle(bx, by, bx + measure_w_ext + 8, by + measure_h_ext + 8, false);
draw_set_color(c_white);
gm82font_draw_text_ext(bx + 4, by + 4, sample_wrap, 28, 180);

draw_set_halign(fa_center);
draw_set_valign(fa_middle);
draw_set_color(c_yellow);
draw_circle(360, 190, 3, false);
gm82font_draw_text_transformed(360, 190, sample_transform, 1.2, 1.2, angle);

draw_set_halign(fa_left);
draw_set_valign(fa_top);
gm82font_draw_text_color(16, 300, sample_color, c_red, c_yellow, c_lime, c_aqua, 1);

draw_set_halign(fa_center);
draw_set_valign(fa_middle);
draw_set_color(c_white);
draw_circle(470, 340, 3, false);
gm82font_draw_text_ext_transformed_colour(
    470,
    340,
    sample_ext_transform,
    28,
    190,
    1.0,
    1.0,
    -12,
    c_white,
    c_yellow,
    c_orange,
    c_aqua,
    1
);
