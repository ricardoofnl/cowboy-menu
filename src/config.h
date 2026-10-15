#pragma once

// persist g_feat + menu scrollers to /data/GoldHEN/cowboy_menu.ini
void config_load(void);   // call once at start; no-op if the file is absent
void config_save(void);   // write the current toggle/scroller state
void config_delete(void); // remove the file (does not touch live state)
