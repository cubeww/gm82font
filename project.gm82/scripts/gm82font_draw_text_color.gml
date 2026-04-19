external_call(global.gm82font_fn_set_text_transform, -1, -1, 1, 1, 0);
external_call(global.gm82font_fn_set_text_gradient, argument3, argument4, argument5, argument6, argument7);
return external_call(global.gm82font_fn_draw_text_ext_transformed_color, gm82font_draw_get_font(), argument0, argument1, argument2);
