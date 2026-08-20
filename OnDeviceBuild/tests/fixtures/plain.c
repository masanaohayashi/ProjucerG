/* Valid C, invalid C++ (`new` is a keyword). Must not receive -std=gnu++17. */
int new = 1;
int ondevice_c_marker (void) { return new; }
