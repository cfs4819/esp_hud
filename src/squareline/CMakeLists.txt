SET(SOURCES ui_comp_dropdown.c
    ui_comp_roller.c
    ui_comp.c
    ui_Home.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_img_bg5_png.c
    ui_img_map_demo2_png.c
    ui_img_gps_bg_png.c
    ui_img_speedbg_small_png.c
    ui_img_speed_fg_small_png.c
    fonts/ui_font_Big.c
    fonts/ui_font_Medium.c
    fonts/ui_font_Number.c
    fonts/ui_font_Small.c)

add_library(ui ${SOURCES})
