from pathlib import Path
s=Path('C3M1_Camera/Core/Src/main.c').read_text()
boot=s.split('int main(void)',1)[1]
reset=boot.split('if (change & C3_NAV_RESET)',1)[1].split('if (change & C3_NAV_FORMAT)',1)[0]
assert reset.index('c3_settings_flash_save(&defaults)') < reset.index('settings=defaults') < reset.index('if (choose_language(&settings,1u))') < reset.index('c3_startup_hint_arm(&startup_hint)') < reset.index('resume_preview(&camera_ok')
assert boot.index('(void)choose_language(&settings,1u)') < boot.index('c3_startup_hint_t startup_hint={0}') < boot.index('c3_startup_hint_frame(&startup_hint')
assert boot.index('c3_camera_preview_draw(&settings') < boot.index('c3_startup_hint_frame(&startup_hint')
assert 'boot_errors_before==c3_live_errors' in boot.split('c3_startup_hint_frame(&startup_hint',1)[1].split('c3_ui_draw_startup_hint()',1)[0]
assert s.count('c3_startup_hint_arm(&startup_hint)')==1
print('PASS startup/reset source ordering: mandatory selection, successful save, re-arm, first valid preview; ordinary language change does not re-arm')
