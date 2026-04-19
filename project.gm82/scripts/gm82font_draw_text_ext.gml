external_call(global.gm82font_fn_set_text_transform, argument3, argument4, 1, 1, 0);
external_call(global.gm82font_fn_set_text_gradient, draw_get_color(), draw_get_color(), draw_get_color(), draw_get_color(), draw_get_alpha());
return external_call(global.gm82font_fn_draw_text_ext_transformed_color, gm82font_font_get_font(), argument0, argument1, argument2);
