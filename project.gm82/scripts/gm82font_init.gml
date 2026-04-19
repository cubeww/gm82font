global.gm82font_dll = "gm82font.dll";
global.gm82font_calltype = 0;
global.gm82font_ty_real = 0;
global.gm82font_ty_string = 1;
global.gm82font_current_font = -1;

global.gm82font_fn_last_error = external_define(global.gm82font_dll, "gm82font_last_error", global.gm82font_calltype, global.gm82font_ty_string, 0);
global.gm82font_fn_font_add = external_define(global.gm82font_dll, "gm82font_font_add", global.gm82font_calltype, global.gm82font_ty_real, 2, global.gm82font_ty_string, global.gm82font_ty_real);
global.gm82font_fn_font_delete = external_define(global.gm82font_dll, "gm82font_font_delete", global.gm82font_calltype, global.gm82font_ty_real, 1, global.gm82font_ty_real);
global.gm82font_fn_string_width = external_define(global.gm82font_dll, "gm82font_string_width", global.gm82font_calltype, global.gm82font_ty_real, 2, global.gm82font_ty_real, global.gm82font_ty_string);
global.gm82font_fn_string_width_ext = external_define(global.gm82font_dll, "gm82font_string_width_ext", global.gm82font_calltype, global.gm82font_ty_real, 4, global.gm82font_ty_real, global.gm82font_ty_string, global.gm82font_ty_real, global.gm82font_ty_real);
global.gm82font_fn_string_height = external_define(global.gm82font_dll, "gm82font_string_height", global.gm82font_calltype, global.gm82font_ty_real, 2, global.gm82font_ty_real, global.gm82font_ty_string);
global.gm82font_fn_string_height_ext = external_define(global.gm82font_dll, "gm82font_string_height_ext", global.gm82font_calltype, global.gm82font_ty_real, 4, global.gm82font_ty_real, global.gm82font_ty_string, global.gm82font_ty_real, global.gm82font_ty_real);
global.gm82font_fn_set_text_transform = external_define(global.gm82font_dll, "gm82font_set_text_transform", global.gm82font_calltype, global.gm82font_ty_real, 5, global.gm82font_ty_real, global.gm82font_ty_real, global.gm82font_ty_real, global.gm82font_ty_real, global.gm82font_ty_real);
global.gm82font_fn_set_text_gradient = external_define(global.gm82font_dll, "gm82font_set_text_gradient", global.gm82font_calltype, global.gm82font_ty_real, 5, global.gm82font_ty_real, global.gm82font_ty_real, global.gm82font_ty_real, global.gm82font_ty_real, global.gm82font_ty_real);
global.gm82font_fn_draw_text_ext_transformed_color = external_define(global.gm82font_dll, "gm82font_draw_text_ext_transformed_color", global.gm82font_calltype, global.gm82font_ty_real, 4, global.gm82font_ty_real, global.gm82font_ty_real, global.gm82font_ty_real, global.gm82font_ty_string);

return
    (global.gm82font_fn_last_error >= 0) &&
    (global.gm82font_fn_font_add >= 0) &&
    (global.gm82font_fn_font_delete >= 0) &&
    (global.gm82font_fn_string_width >= 0) &&
    (global.gm82font_fn_string_width_ext >= 0) &&
    (global.gm82font_fn_string_height >= 0) &&
    (global.gm82font_fn_string_height_ext >= 0) &&
    (global.gm82font_fn_set_text_transform >= 0) &&
    (global.gm82font_fn_set_text_gradient >= 0) &&
    (global.gm82font_fn_draw_text_ext_transformed_color >= 0);
