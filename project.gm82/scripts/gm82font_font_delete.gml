if (global.gm82font_current_font == argument0) {
    global.gm82font_current_font = -1;
}
return external_call(global.gm82font_fn_font_delete, argument0);
