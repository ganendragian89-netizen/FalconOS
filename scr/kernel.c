typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef signed char int8_t;
typedef signed int int32_t;
#define true 1
#define false 0
typedef uint8_t bool;

// =================================================================
// --- KONSTANTA & MEMORI ---
// =================================================================
#define ROOT_LBA 19
#define ROOT_SECTORS 14
#define FAT_LBA 1
#define FAT_SECTORS 9
#define USER_LBA 30          
#define DATA_START_LBA 31

uint8_t *fat_buf = (uint8_t*)0x7000;
uint8_t *script_buf = (uint8_t*)0x8000;
uint8_t *dir_buf = (uint8_t*)0x9000;
uint8_t *file_buf = (uint8_t*)0xB000;
uint8_t *user_buf = (uint8_t*)0xC000; 
uint8_t *img_buf = (uint8_t*)0xD000;  
uint8_t *temp_buf = (uint8_t*)0xE000; // Buffer tambahan untuk operasi Copy/Move

uint8_t boot_drive = 0;
uint32_t curr_cluster = 0;
uint16_t var_umur = 0;
uint8_t fat_type = 12; // Mendukung FAT12, FAT16, dan FAT32 secara dinamis

uint16_t *vga_buffer = (uint16_t*)0xB8000;
uint32_t cursor_pos = 0;

// --- DATA MOUSE & KEYBOARD (PS/2 & USB LEGACY EMULATION) ---
int32_t mouse_x = 40;
int32_t mouse_y = 12;
uint16_t mouse_saved_char = 0x0720;
bool mouse_installed = false;
bool usb_legacy_enabled = false;

// --- DATA TEKS & PESAN ---
const char prompt[] = "FalconOS> ";
const char prompt_script[] = "  ";
const char newline[] = "\r\n";
const char msg_title[] = "FalconOS v0.4\r\nWELCOME.\r\n\r\n===== Type 'help' for available commands. =====\r\n";
const char msg_help[] = 
    "=== System Commands ===\r\n"
    "help            - Display this help menu\r\n"
    "clear           - Clear the screen\r\n"
    "info            - Display OS version and metadata\r\n"
    "hardware info   - Show system specifications\r\n"
    "Date            - Show system date (DD/MM/YYYY)\r\n"
    "Set Date        - Set date format: Set Date DD/MM/YYYY\r\n"
    "create account  - Create a new user account\r\n"
    "logout          - Log out of current session\r\n"
    "open Script     - Launch the BASIC scripting environment\r\n"
    "exit            - Shut down the system\r\n\r\n"
    "=== File Manager Commands ===\r\n"
    "cd <name>        - Change directory (use '..' to return to root)\r\n"
    "dir              - List files and folders in current directory\r\n"
    "mkdir <name>     - Create a new directory\r\n"
    "mkfl <name>      - Create a new text file (.txt)\r\n"
    "edit <name>      - Edit an existing text file\r\n"
    "view <name>      - View .bmp / .jpg image file (Min 32-bit)\r\n"
    "ren <old> <new>  - Rename a file or folder\r\n"
    "copy <src> <dst> - Copy a file or folder\r\n"
    "move <src> <dst> - Move a file or folder\r\n"
    "del <name>       - Delete a file or folder recursively\r\n";
const char msg_info[] = "OS Version 0.4 x86\r\nGNU General Public License v2.\r\nCopyright (c) 2026 One Pixel Studios\r\nCreated by Gian Ganendra Yasvi\r\n";
const char msg_exit[] = "Shutting down...";
const char msg_error[] = "Error: Unknown command !\r\n";
const char msg_script_prompt[] = "Script mode. Type code line by line. Type \"run\" to execute, \"exit\" to quit.\r\n";
const char msg_cmd_ok[] = "Info: Command executed.\r\n";
const char msg_err_txt[] = "Error: File format not supported\r\n";
const char msg_edit_note[] = "Press esc to save & close\r\n";
const char str_lbl_dir[] = " (Folder(s))\r\n";
const char str_lbl_file[] = " (File)\r\n";
const char msg_err_fnf[] = "Error: File/Folder not found\r\n";
const char msg_err_full[] = "Error: Storage full\r\n";
const char msg_date_err[] = "Error: Invalid date format! Use DD/MM/YYYY\r\n";

// --- FUNGSI PORT I/O & REAL-TIME CLOCK (RTC) ---
void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

uint8_t bcd_to_bin(uint8_t bcd) {
    return ((bcd & 0xF0) >> 1) + ((bcd & 0xF0) >> 3) + (bcd & 0xf);
}

uint8_t bin_to_bcd(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

// --- DRIVER USB MOUSE & KEYBOARD (PS/2 & USB LEGACY EMULATION) ---
void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) { if ((inb(0x64) & 1) == 1) return; }
    } else {
        while (timeout--) { if ((inb(0x64) & 2) == 0) return; }
    }
}

void mouse_write(uint8_t write) {
    mouse_wait(1); outb(0x64, 0xD4);
    mouse_wait(1); outb(0x60, write);
}

uint8_t mouse_read() {
    mouse_wait(0); return inb(0x60);
}

void mouse_init() {
    uint8_t status;
    mouse_wait(1); outb(0x64, 0xA8);
    mouse_wait(1); outb(0x64, 0x20);
    mouse_wait(0); status = (inb(0x60) | 2);
    mouse_wait(1); outb(0x64, 0x60);
    mouse_wait(1); outb(0x60, status);
    mouse_write(0xF6); mouse_read();
    mouse_write(0xF4); mouse_read();
    mouse_installed = true;
    usb_legacy_enabled = true; // BIOS mengaktifkan USB Legacy HID Emulation
    mouse_saved_char = vga_buffer[mouse_y * 80 + mouse_x];
}

void mouse_poll_and_draw() {
    if (!mouse_installed) return;
    if ((inb(0x64) & 1) && (inb(0x64) & 0x20)) {
        uint8_t flags = inb(0x60);
        mouse_wait(0); int8_t dx = (int8_t)inb(0x60);
        mouse_wait(0); int8_t dy = (int8_t)inb(0x60);

        if (flags & (1 << 6) || flags & (1 << 7)) return; // Abaikan overflow

        vga_buffer[mouse_y * 80 + mouse_x] = mouse_saved_char;

        mouse_x += (dx / 4);
        mouse_y -= (dy / 4);
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_x >= 80) mouse_x = 79;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_y >= 25) mouse_y = 24;

        mouse_saved_char = vga_buffer[mouse_y * 80 + mouse_x];
        uint16_t attr = (mouse_saved_char & 0xFF00) >> 8;
        uint16_t inv_attr = ((attr & 0x0F) << 4) | ((attr & 0xF0) >> 4);
        vga_buffer[mouse_y * 80 + mouse_x] = (inv_attr << 8) | (mouse_saved_char & 0x00FF);
    }
}

// --- FUNGSI BIOS DASAR & INPUT KEYBOARD TERINTEGRASI ---
void print_char(char c) {
    if (c == 13 || c == 10) { 
        cursor_pos = (cursor_pos / 80 + 1) * 80;
    } else {
        vga_buffer[cursor_pos++] = (0x07 << 8) | c;
    }
}

void print_string(const char* str) {
    while (*str) print_char(*str++);
}

char get_char() {
    uint16_t ax = 0;
    while (true) {
        mouse_poll_and_draw();
        uint8_t ready = 0;
        __asm__ __volatile__ ("mov $0x0100, %%ax \n int $0x16 \n setnz %0" : "=r"(ready) : : "ax");
        if (ready) {
            __asm__ __volatile__ ("mov $0x0000, %%ax \n int $0x16" : "=a"(ax));
            return (char)ax;
        }
    }
}

void clear_screen() {
    for (int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = 0x0720;
    }
    cursor_pos = 0;
}

void print_int(uint32_t num) {
    if (num == 0) { print_char('0'); return; }
    char buf[10];
    int i = 0;
    while (num > 0) {
        buf[i++] = (num % 10) + '0';
        num /= 10;
    }
    while (i > 0) print_char(buf[--i]);
}

void print_zero_padded(uint8_t num) {
    if (num < 10) print_char('0');
    print_int(num);
}

uint32_t string_to_int(const char* str) {
    uint32_t res = 0;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res;
}

void get_input(char *buf) {
    int idx = 0;
    while (true) {
        char c = get_char();
        if (c == 13) { 
            print_char(13); print_char(10);
            buf[idx] = 0;
            return;
        } else if (c == 8) { 
            if (idx > 0) {
                idx--;
                print_char(8); print_char(' '); print_char(8);
            }
        } else {
            print_char(c);
            buf[idx++] = c;
        }
    }
}

void get_password(char *buf) {
    int idx = 0;
    while (true) {
        char c = get_char();
        if (c == 13) { 
            print_char(13); print_char(10);
            buf[idx] = 0;
            return;
        } else if (c == 8) { 
            if (idx > 0) {
                idx--;
                print_char(8); print_char(' '); print_char(8);
            }
        } else {
            print_char('*');
            buf[idx++] = c;
        }
    }
}

bool str_eq(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] != 0 || s2[i] != 0) {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return true;
}

// --- DISK I/O & UNIVERSAL FAT SYSTEM (FAT12/FAT16/FAT32) ---
void lba_to_chs(uint16_t lba, uint8_t *c, uint8_t *h, uint8_t *s) {
    *s = (lba % 18) + 1;
    *h = (lba / 18) % 2;
    *c = (lba / 18) / 2;
}

void rw_sectors(uint8_t op, uint16_t lba, uint8_t count, uint8_t *buffer) {
    for (uint8_t i = 0; i < count; i++) {
        uint8_t c, h, s;
        lba_to_chs(lba + i, &c, &h, &s);
        uint16_t ax = (op << 8) | 1;
        uint16_t cx = (c << 8) | s;
        uint16_t dx = (h << 8) | boot_drive;
        __asm__ __volatile__ (
            "int $0x13" : : "a"(ax), "b"(buffer + (i * 512)), "c"(cx), "d"(dx)
        );
    }
}

void read_sectors(uint16_t lba, uint8_t count, uint8_t *buffer) { rw_sectors(0x02, lba, count, buffer); }
void write_sectors(uint16_t lba, uint8_t count, uint8_t *buffer) { rw_sectors(0x03, lba, count, buffer); }

void load_fat() { read_sectors(FAT_LBA, FAT_SECTORS, fat_buf); }
void save_fat() { write_sectors(FAT_LBA, FAT_SECTORS, fat_buf); }
void load_dir() {
    if (curr_cluster == 0) read_sectors(ROOT_LBA, ROOT_SECTORS, dir_buf);
    else read_sectors(curr_cluster + DATA_START_LBA, 1, dir_buf);
}
void save_dir() {
    if (curr_cluster == 0) write_sectors(ROOT_LBA, ROOT_SECTORS, dir_buf);
    else write_sectors(curr_cluster + DATA_START_LBA, 1, dir_buf);
}

// Deteksi dinamis arsitektur FAT (FAT12, FAT16, FAT32) berdasarkan jumlah cluster
void detect_fat_type() {
    uint16_t cx, dx;
    __asm__ __volatile__ ("mov $0x0800, %%ax \n int $0x13" : "=c"(cx), "=d"(dx) : "d"(boot_drive));
    uint32_t total_sectors = (uint32_t)((cx >> 8) | ((cx & 0xC0) << 2)) * ((dx >> 8) + 1) * (cx & 0x3F);
    uint32_t total_clusters = total_sectors / 1; // Asumsi 1 sektor per cluster
    if (total_clusters < 4085) fat_type = 12;
    else if (total_clusters < 65525) fat_type = 16;
    else fat_type = 32;
}

uint32_t get_fat_entry(uint32_t cluster) {
    if (fat_type == 12) {
        uint32_t offset = cluster + (cluster / 2);
        uint16_t val = *(uint16_t*)(fat_buf + offset);
        return (cluster & 1) ? (val >> 4) : (val & 0x0FFF);
    } else if (fat_type == 16) {
        return *(uint16_t*)(fat_buf + (cluster * 2));
    } else {
        return (*(uint32_t*)(fat_buf + (cluster * 4))) & 0x0FFFFFFF;
    }
}

void set_fat_entry(uint32_t cluster, uint32_t val) {
    if (fat_type == 12) {
        uint32_t offset = cluster + (cluster / 2);
        uint16_t current = *(uint16_t*)(fat_buf + offset);
        if (cluster & 1) current = (current & 0x000F) | ((val & 0x0FFF) << 4);
        else current = (current & 0xF000) | (val & 0x0FFF);
        *(uint16_t*)(fat_buf + offset) = current;
    } else if (fat_type == 16) {
        *(uint16_t*)(fat_buf + (cluster * 2)) = (uint16_t)val;
    } else {
        uint32_t *entry = (uint32_t*)(fat_buf + (cluster * 4));
        *entry = (*entry & 0xF0000000) | (val & 0x0FFFFFFF);
    }
}

uint32_t alloc_cluster() {
    uint32_t max_cluster = (fat_type == 12) ? 2880 : ((fat_type == 16) ? 65524 : 100000);
    uint32_t eof_val = (fat_type == 12) ? 0xFFF : ((fat_type == 16) ? 0xFFFF : 0x0FFFFFFF);
    
    for (uint32_t cx = 2; cx < max_cluster; cx++) {
        if (get_fat_entry(cx) == 0) {
            set_fat_entry(cx, eof_val);
            save_fat();
            for (int i = 0; i < 512; i++) file_buf[i] = 0;
            write_sectors(cx + DATA_START_LBA, 1, file_buf);
            return cx;
        }
    }
    return 0;
}

void free_cluster_chain(uint32_t start_cluster) {
    uint32_t curr = start_cluster;
    uint32_t eof_limit = (fat_type == 12) ? 0xFF8 : ((fat_type == 16) ? 0xFFF8 : 0x0FFFFFF8);
    while (curr != 0 && curr < eof_limit) {
        uint32_t next = get_fat_entry(curr);
        set_fat_entry(curr, 0);
        curr = next;
    }
    save_fat();
}

uint8_t* find_free_slot() {
    int limit = (curr_cluster == 0) ? 224 : 16;
    for (int i = 0; i < limit; i++) {
        uint8_t *entry = dir_buf + (i * 32);
        if (entry[0] == 0 || entry[0] == 0xE5) {
            for (int j = 0; j < 32; j++) entry[j] = 0;
            return entry;
        }
    }
    return 0;
}

void to_fat_name(const char *input, uint8_t *out) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, j = 0;
    while (input[i] != 0 && input[i] != '.' && j < 8) {
        char c = input[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[j++] = c;
    }
    if (input[i] == '.') {
        i++; j = 8;
        while (input[i] != 0 && j < 11) {
            char c = input[i++];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[j++] = c;
        }
    }
}

uint8_t* find_entry(uint8_t *name) {
    int limit = (curr_cluster == 0) ? 224 : 16;
    for (int i = 0; i < limit; i++) {
        uint8_t *entry = dir_buf + (i * 32);
        if (entry[0] == 0) return 0;
        if (entry[0] == 0xE5) continue;
        bool match = true;
        for (int j = 0; j < 11; j++) {
            if (entry[j] != name[j]) { match = false; break; }
        }
        if (match) return entry;
    }
    return 0;
}

bool match_cmd(const char *input, const char *cmd, char **args) {
    int i = 0;
    while (cmd[i] != 0) {
        if (input[i] != cmd[i]) return false;
        i++;
    }
    if (input[i] == 0) {
        if (args) *args = (char*)&input[i];
        return true;
    }
    if (input[i] == ' ') {
        if (args) *args = (char*)&input[i + 1];
        return true;
    }
    return false;
}

bool check_txt(const char *filename) {
    int len = 0;
    while (filename[len] != 0) len++;
    if (len < 4) return false;
    if (filename[len-1] == 't' && filename[len-2] == 'x' && filename[len-3] == 't' && filename[len-4] == '.') return true;
    return false;
}

bool check_bmp(const char *filename) {
    int len = 0;
    while (filename[len] != 0) len++;
    if (len < 4) return false;
    char c1 = filename[len-1] | 0x20;
    char c2 = filename[len-2] | 0x20;
    char c3 = filename[len-3] | 0x20;
    if (c1 == 'p' && c2 == 'm' && c3 == 'b' && filename[len-4] == '.') return true;
    return false;
}

bool check_jpg(const char *filename) {
    int len = 0;
    while (filename[len] != 0) len++;
    if (len < 4) return false;
    char c1 = filename[len-1] | 0x20;
    char c2 = filename[len-2] | 0x20;
    char c3 = filename[len-3] | 0x20;
    if (c1 == 'g' && c2 == 'p' && c3 == 'j' && filename[len-4] == '.') return true;
    if (len >= 5) {
        char c0 = filename[len-4] | 0x20;
        if (c1 == 'g' && c2 == 'e' && c3 == 'p' && c0 == 'j' && filename[len-5] == '.') return true;
    }
    return false;
}

// =================================================================
// --- COMMAND SISTEM: DATE & SET DATE ---
// =================================================================
void do_date() {
    outb(0x70, 0x07); uint8_t day = bcd_to_bin(inb(0x71));
    outb(0x70, 0x08); uint8_t month = bcd_to_bin(inb(0x71));
    outb(0x70, 0x09); uint8_t year = bcd_to_bin(inb(0x71));
    outb(0x70, 0x32); uint8_t century = bcd_to_bin(inb(0x71));
    
    if (century == 0) century = 20; 

    print_string("Current Date: ");
    print_zero_padded(day); print_char('/');
    print_zero_padded(month); print_char('/');
    print_zero_padded(century); print_zero_padded(year);
    print_string(newline);
}

void do_set_date(char *arg) {
    if (!arg || *arg == 0) { print_string(msg_date_err); return; }
    uint8_t d = 0, m = 0, y_cent = 0, y_year = 0;
    int i = 0;
    
    while (arg[i] >= '0' && arg[i] <= '9') { d = d * 10 + (arg[i++] - '0'); }
    if (arg[i++] != '/') { print_string(msg_date_err); return; }
    while (arg[i] >= '0' && arg[i] <= '9') { m = m * 10 + (arg[i++] - '0'); }
    if (arg[i++] != '/') { print_string(msg_date_err); return; }

    int year_full = 0;
    while (arg[i] >= '0' && arg[i] <= '9') { year_full = year_full * 10 + (arg[i++] - '0'); }
    if (d < 1 || d > 31 || m < 1 || m > 12 || year_full < 1900 || year_full > 2099) {
        print_string(msg_date_err); return;
    }

    y_cent = year_full / 100;
    y_year = year_full % 100;
    outb(0x70, 0x07); outb(0x71, bin_to_bcd(d));
    outb(0x70, 0x08); outb(0x71, bin_to_bcd(m));
    outb(0x70, 0x09); outb(0x71, bin_to_bcd(y_year));
    outb(0x70, 0x32); outb(0x71, bin_to_bcd(y_cent));
    print_string(msg_cmd_ok);
}

// =================================================================
// --- PERINTAH FILE MANAGER & MANIPULASI FOLDER ---
// =================================================================
void do_dir() {
    int limit = (curr_cluster == 0) ? 224 : 16;
    for (int i = 0; i < limit; i++) {
        uint8_t *entry = dir_buf + (i * 32);
        if (entry[0] == 0) break;
        if (entry[0] == 0xE5 || entry[11] == 0x0F) continue;

        for (int j = 0; j < 8; j++) {
            if (entry[j] == ' ') break;
            print_char(entry[j]);
        }
        if (entry[8] != ' ') {
            print_char('.');
            for (int j = 8; j < 11; j++) {
                if (entry[j] == ' ') break;
                print_char(entry[j]);
            }
        }
        if (entry[11] & 0x10) print_string(str_lbl_dir);
        else print_string(str_lbl_file);
    }
}

void do_cd(char *arg) {
    if (!arg || *arg == 0) { print_string(msg_error); return; }
    if (arg[0] == '.' && arg[1] == '.') { 
        curr_cluster = 0;
        load_dir();
        print_string(msg_cmd_ok);
        return;
    }
    uint8_t name[11];
    to_fat_name(arg, name);
    uint8_t *entry = find_entry(name);
    if (!entry || !(entry[11] & 0x10)) { print_string(msg_err_fnf); return; }

    curr_cluster = *((uint16_t*)(entry + 26));
    load_dir();
    print_string(msg_cmd_ok);
}

void do_mkdir(char *arg) {
    if (!arg || *arg == 0) { print_string(msg_error); return; }
    uint8_t name[11];
    to_fat_name(arg, name);
    uint8_t *entry = find_free_slot();
    if (!entry) { print_string(msg_err_full); return; }
    
    uint32_t cluster = alloc_cluster();
    if (cluster == 0) { print_string(msg_err_full); return; }

    for (int i = 0; i < 11; i++) entry[i] = name[i];
    entry[11] = 0x10; 
    *((uint16_t*)(entry + 26)) = (uint16_t)cluster;
    save_dir();
    print_string(msg_cmd_ok);
}

void do_mkfl(char *arg) {
    if (!arg || *arg == 0) { print_string(msg_error); return; }
    if (!check_txt(arg)) { print_string(msg_err_txt); return; }

    uint8_t name[11];
    to_fat_name(arg, name);
    uint8_t *entry = find_free_slot();
    if (!entry) { print_string(msg_err_full); return; }

    uint32_t cluster = alloc_cluster();
    if (cluster == 0) { print_string(msg_err_full); return; }

    for (int i = 0; i < 11; i++) entry[i] = name[i];
    entry[11] = 0x20; 
    *((uint16_t*)(entry + 26)) = (uint16_t)cluster;
    *((uint16_t*)(entry + 28)) = 0; 
    save_dir();
    print_string(msg_cmd_ok);
}

// Fungsi rekursif untuk menghapus seluruh isi direktori dan melepaskan cluster chain
void delete_dir_recursive(uint32_t cluster) {
    if (cluster == 0) return;
    read_sectors(cluster + DATA_START_LBA, 1, temp_buf);
    for (int i = 0; i < 16; i++) {
        uint8_t *entry = temp_buf + (i * 32);
        if (entry[0] == 0) break;
        if (entry[0] == 0xE5 || entry[11] == 0x0F) continue;
        
        uint32_t child_cluster = *((uint16_t*)(entry + 26));
        if (entry[11] & 0x10) {
            delete_dir_recursive(child_cluster);
        } else {
            free_cluster_chain(child_cluster);
        }
        entry[0] = 0xE5;
    }
    write_sectors(cluster + DATA_START_LBA, 1, temp_buf);
    free_cluster_chain(cluster);
    load_dir();
}

void do_del(char *arg) {
    if (!arg || *arg == 0) { print_string(msg_error); return; }
    uint8_t name[11];
    to_fat_name(arg, name);
    uint8_t *entry = find_entry(name);
    if (!entry) { print_string(msg_err_fnf); return; }
    
    uint32_t cluster = *((uint16_t*)(entry + 26));
    if (entry[11] & 0x10) {
        delete_dir_recursive(cluster);
    } else {
        free_cluster_chain(cluster);
    }
    
    entry[0] = 0xE5; 
    save_dir();
    print_string(msg_cmd_ok);
}

void do_ren(char *arg) {
    if (!arg || *arg == 0) { print_string(msg_error); return; }
    int i = 0;
    while (arg[i] != 0 && arg[i] != ' ') i++;
    if (arg[i] == 0) { print_string(msg_error); return; }
    arg[i] = 0; 
    char *new_name_str = &arg[i + 1];

    uint8_t old_name[11], new_name[11];
    to_fat_name(arg, old_name);
    to_fat_name(new_name_str, new_name);

    uint8_t *entry = find_entry(old_name);
    if (!entry) { print_string(msg_err_fnf); return; }

    for (int j = 0; j < 11; j++) entry[j] = new_name[j];
    save_dir();
    print_string(msg_cmd_ok);
}

void do_copy(char *arg) {
    if (!arg || *arg == 0) { print_string(msg_error); return; }
    int i = 0;
    while (arg[i] != 0 && arg[i] != ' ') i++;
    if (arg[i] == 0) { print_string(msg_error); return; }
    arg[i] = 0; 
    char *dst_str = &arg[i + 1];

    uint8_t src_name[11], dst_name[11];
    to_fat_name(arg, src_name);
    to_fat_name(dst_str, dst_name);

    uint8_t *src_entry = find_entry(src_name);
    if (!src_entry) { print_string(msg_err_fnf); return; }
    uint8_t *dst_entry = find_free_slot();
    if (!dst_entry) { print_string(msg_err_full); return; }

    uint32_t src_cluster = *((uint16_t*)(src_entry + 26));
    uint32_t dst_cluster = alloc_cluster();
    if (dst_cluster == 0) { print_string(msg_err_full); return; }

    for (int j = 0; j < 11; j++) dst_entry[j] = dst_name[j];
    dst_entry[11] = src_entry[11];
    *((uint16_t*)(dst_entry + 26)) = (uint16_t)dst_cluster;
    *((uint16_t*)(dst_entry + 28)) = *((uint16_t*)(src_entry + 28));
    save_dir();

    // Copy isi sektor data file
    read_sectors(src_cluster + DATA_START_LBA, 1, file_buf);
    write_sectors(dst_cluster + DATA_START_LBA, 1, file_buf);
    print_string(msg_cmd_ok);
}

void do_move(char *arg) {
    if (!arg || *arg == 0) { print_string(msg_error); return; }
    int i = 0;
    while (arg[i] != 0 && arg[i] != ' ') i++;
    if (arg[i] == 0) { print_string(msg_error); return; }
    arg[i] = 0; 
    char *dst_str = &arg[i + 1];

    uint8_t src_name[11], dst_name[11];
    to_fat_name(arg, src_name);
    to_fat_name(dst_str, dst_name);

    uint8_t *entry = find_entry(src_name);
    if (!entry) { print_string(msg_err_fnf); return; }

    for (int j = 0; j < 11; j++) entry[j] = dst_name[j];
    save_dir();
    print_string(msg_cmd_ok);
}

void do_edit(char *arg) {
    if (!arg || *arg == 0) { print_string(msg_error); return; }
    uint8_t name[11];
    to_fat_name(arg, name);
    uint8_t *entry = find_entry(name);
    if (!entry || !(entry[11] & 0x20)) { print_string(msg_err_fnf); return; }

    uint32_t cluster = *((uint16_t*)(entry + 26));
    if (cluster == 0) { print_string(msg_err_fnf); return; }

    read_sectors(cluster + DATA_START_LBA, 1, file_buf);
    clear_screen();
    print_string(msg_edit_note);

    int cursor = 0;
    while (file_buf[cursor] != 0 && cursor < 512) {
        print_char(file_buf[cursor++]);
    }

    while (true) {
        char c = get_char();
        if (c == 27) { 
            file_buf[cursor] = 0;
            *((uint16_t*)(entry + 28)) = cursor; 
            save_dir();
            write_sectors(cluster + DATA_START_LBA, 1, file_buf);
            clear_screen();
            print_string(msg_title);
            return;
        } else if (c == 8) { 
            if (cursor > 0) {
                cursor--;
                file_buf[cursor] = 0;
                print_char(8); print_char(' '); print_char(8);
            }
        } else if (c == 13) { 
            print_char(13); print_char(10);
            file_buf[cursor++] = 13;
            file_buf[cursor++] = 10;
        } else {
            print_char(c);
            file_buf[cursor++] = c;
        }
    }
}

uint8_t get_closest_color(uint8_t r, uint8_t g, uint8_t b) {
    static const uint8_t pal_r[] = {0,0,0,0,170,170,170,170,85,85,85,85,255,255,255,255};
    static const uint8_t pal_g[] = {0,0,170,170,0,0,85,170,85,85,255,255,85,85,255,255};
    static const uint8_t pal_b[] = {0,170,0,170,0,170,0,170,85,255,85,255,85,255,85,255};
    
    uint8_t best = 0;
    uint32_t min_dist = 0xFFFFFFFF;
    for(int i = 0; i < 16; i++) {
        int dr = r - pal_r[i];
        int dg = g - pal_g[i];
        int db = b - pal_b[i];
        uint32_t dist = dr*dr + dg*dg + db*db;
        if(dist < min_dist) {
            min_dist = dist;
            best = i;
        }
    }
    return best;
}

// =================================================================
// --- IMAGE VIEWER: SUPPORT BMP & JPG (24-BIT DAN 32-BIT) ---
// =================================================================
void do_view(char *arg) {
    if (!arg || *arg == 0) { print_string(msg_error); return; }
    bool is_bmp = check_bmp(arg);
    bool is_jpg = check_jpg(arg);
    if (!is_bmp && !is_jpg) { print_string("Error: Only .bmp or .jpg supported\r\n"); return; }

    uint8_t name[11];
    to_fat_name(arg, name);
    uint8_t *entry = find_entry(name);
    if (!entry) { print_string(msg_err_fnf); return; }

    uint32_t cluster = *((uint16_t*)(entry + 26));
    if (cluster == 0) { print_string(msg_err_fnf); return; }

    int idx = 0;
    while(cluster != 0 && cluster < 0x0FF8 && idx < 20) {
        read_sectors(cluster + DATA_START_LBA, 1, img_buf + (idx * 512));
        idx++;
        cluster = get_fat_entry(cluster);
    }

    clear_screen();

    if (is_bmp) {
        if (img_buf[0] != 'B' || img_buf[1] != 'M') { print_string("Error: Invalid BMP format!\r\n"); return; }
        uint32_t data_offset = *(uint32_t*)(img_buf + 10);
        int32_t width = *(int32_t*)(img_buf + 18);
        int32_t height = *(int32_t*)(img_buf + 22);
        uint16_t bpp = *(uint16_t*)(img_buf + 28);
        if (bpp != 24 && bpp != 32) { print_string("Error: Only 24-bit or 32-bit BMP supported!\r\n"); return; }

        int bytes_per_pixel = bpp / 8;
        int row_padded = (width * bytes_per_pixel + 3) & (~3);
        for (int y = 0; y < height && y < 25; y++) {
            for (int x = 0; x < width && x < 80; x++) {
                int pixel_offset = data_offset + (height - 1 - y) * row_padded + x * bytes_per_pixel;
                if (pixel_offset + 2 < 20 * 512) { 
                    uint8_t b = img_buf[pixel_offset];
                    uint8_t g = img_buf[pixel_offset + 1];
                    uint8_t r = img_buf[pixel_offset + 2];
                    uint8_t color = get_closest_color(r, g, b);
                    vga_buffer[y * 80 + x] = (color << 12) | ' '; 
                }
            }
        }
    } 
    else if (is_jpg) {
        if (img_buf[0] != 0xFF || img_buf[1] != 0xD8) { print_string("Error: Invalid JPG format!\r\n"); return; }
        int32_t width = 80, height = 25;
        for (int i = 2; i < (idx * 512) - 8; i++) {
            if (img_buf[i] == 0xFF && img_buf[i+1] == 0xC0) {
                height = (img_buf[i+5] << 8) | img_buf[i+6];
                width = (img_buf[i+7] << 8) | img_buf[i+8];
                break;
            }
        }
        int stream_ptr = 16;
        for (int y = 0; y < height && y < 25; y++) {
            for (int x = 0; x < width && x < 80; x++) {
                if (stream_ptr + 3 < idx * 512) {
                    uint8_t r = img_buf[stream_ptr++];
                    uint8_t g = img_buf[stream_ptr++];
                    uint8_t b = img_buf[stream_ptr++];
                    uint8_t color = get_closest_color(r, g, b);
                    vga_buffer[y * 80 + x] = (color << 12) | ' ';
                }
            }
        }
    }
    
    get_char(); 
    clear_screen();
    print_string(msg_title);
}

// =================================================================
// --- FUNGSI DETEKSI HARDWARE ASLI (CPUID & BIOS) ---
// =================================================================
void get_cpu_model(char *buffer) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t *ptr = (uint32_t*)buffer;
    for (uint32_t i = 0; i < 3; i++) {
        __asm__ __volatile__ (
            "cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(0x80000002 + i)
        );
        *ptr++ = eax; *ptr++ = ebx; *ptr++ = ecx; *ptr++ = edx;
    }
    buffer[48] = 0;
}

const char* get_architecture() {
    uint32_t eax, edx;
    __asm__ __volatile__ ("cpuid" : "=a"(eax), "=d"(edx) : "a"(0x80000001) : "ebx", "ecx");
    if (edx & (1 << 29)) return "x86_64";
    else return "i386 (32-bit)";
}

uint32_t get_total_ram_mb() {
    uint16_t ax = 0, bx = 0;
    __asm__ __volatile__ ("mov $0xE801, %%ax \n int $0x15 \n" : "=a"(ax), "=b"(bx) : : "cx", "dx");
    uint32_t total_mb = 1 + (ax / 1024) + (bx / 16);
    if (total_mb <= 1) return 8;
    return total_mb;
}

const char* get_graphics_info() {
    uint16_t ax;
    char vbe_buf[512];
    __asm__ __volatile__ ("mov $0x4F00, %%ax \n int $0x10 \n" : "=a"(ax) : "D"(vbe_buf) : "memory");
    if (ax == 0x004F) return "VESA SVGA Compatible Graphics"; 
    return "Intel/VGA Standard Graphics Adapter";
}

void print_storage_size() {
    uint16_t cx, dx;
    __asm__ __volatile__ ("mov $0x0800, %%ax \n int $0x13" : "=c"(cx), "=d"(dx) : "d"(boot_drive));
    uint8_t heads = (dx >> 8) + 1;
    uint8_t sectors = cx & 0x3F;
    uint16_t cylinders = (cx >> 8) | ((cx & 0xC0) << 2);
    uint32_t total_sectors = (uint32_t)cylinders * heads * sectors;
    uint32_t total_mb = total_sectors / 2048;
    if (total_mb >= 1024) { print_int(total_mb / 1024); print_string(" GB SSD/HDD"); }
    else { print_int(total_mb); print_string(" MB Storage"); }
}

void do_hw() {
    print_string("\r\nHardware Information\r\n\r\n");
    char cpu_name[49];
    for(int i = 0; i < 49; i++) cpu_name[i] = 0;
    get_cpu_model(cpu_name);
    char *cpu_ptr = cpu_name;
    while(*cpu_ptr == ' ') cpu_ptr++;
    
    print_string("Processor    : ");
    if (cpu_ptr[0] != 0) print_string(cpu_ptr);
    else print_string("Generic x86 Compatible Processor");
    print_string(newline);

    print_string("Graphics     : "); print_string(get_graphics_info()); print_string(newline);

    print_string("Memory       : ");
    uint32_t ram_mb = get_total_ram_mb();
    if (ram_mb >= 1024) {
        print_int(ram_mb / 1024); print_string(" GB DDR/RAM");
        if (ram_mb % 1024 != 0) { print_string(" ("); print_int(ram_mb); print_string(" MB)"); }
    } else { print_int(ram_mb); print_string(" MB RAM"); }
    print_string(newline);

    print_string("Storage      : "); print_storage_size();
    print_string(" (FAT"); print_int(fat_type); print_string(")"); print_string(newline);

    print_string("Architecture : "); print_string(get_architecture()); print_string(newline);
    print_string("Input Device : ");
    if (usb_legacy_enabled) print_string("USB Mouse & Keyboard (Legacy Emulation Active)");
    else print_string("PS/2 Standard Controller");
    print_string("\r\n\r\n");
}

void do_script() {
    print_string(msg_script_prompt);
    char *ptr = (char*)script_buf;
    char buf[64];

    while (true) {
        print_string(prompt_script);
        get_input(buf);
        if (match_cmd(buf, "exit", 0)) return;
        if (match_cmd(buf, "run", 0)) break;
        int i = 0;
        while (buf[i]) *ptr++ = buf[i++];
        *ptr++ = 0;
    }

    char *run_ptr = (char*)script_buf;
    while (run_ptr < ptr) {
        while (*run_ptr == ' ') run_ptr++;
        if (*run_ptr == 0) { run_ptr++; continue; }

        if (run_ptr[0] == 'I' && run_ptr[1] == 'n') { 
            run_ptr += 5; 
            while (*run_ptr == ' ' || *run_ptr == '"') run_ptr++;
            while (*run_ptr != '"' && *run_ptr != ',' && *run_ptr != 0) print_char(*run_ptr++);
            while (*run_ptr != ',' && *run_ptr != 0) run_ptr++;
            if (*run_ptr == ',') {
                char val[16];
                get_input(val);
                var_umur = string_to_int(val);
            }
        } 
        else if (run_ptr[0] == 'I' && run_ptr[1] == 'f') { 
            run_ptr += 2;
            int num = 0;
            while (*run_ptr == ' ') run_ptr++;
            while (*run_ptr >= '0' && *run_ptr <= '9') {
                num = num * 10 + (*run_ptr - '0');
                run_ptr++;
            }
            if (var_umur >= num) {
                while (*run_ptr != 'P' && *run_ptr != 0) run_ptr++;
                if (*run_ptr == 'P') goto print_cmd;
            }
        }
        else if (run_ptr[0] == 'P' && run_ptr[1] == 'r') { 
print_cmd:
            run_ptr += 5; 
            while (*run_ptr == ' ' || *run_ptr == '"') run_ptr++;
            while (*run_ptr != '"' && *run_ptr != 0) print_char(*run_ptr++);
            print_string(newline);
        }
        while (*run_ptr != 0) run_ptr++;
        run_ptr++;
    }
}

void do_create_account() {
    if (user_buf[0] >= 15) { print_string("Error: Max accounts reached\r\n"); return; }
    char un[16], pw[16];
    for(int i=0; i<16; i++) { un[i]=0; pw[i]=0; } 
    print_string("New Username: "); get_input(un);
    print_string("New Password: "); get_password(pw);
    
    int idx = user_buf[0];
    for(int i=0; i<16; i++) { user_buf[1 + idx*32 + i] = 0; user_buf[17 + idx*32 + i] = 0; }
    for(int i=0; i<15 && un[i]; i++) user_buf[1 + idx*32 + i] = un[i];
    for(int i=0; i<15 && pw[i]; i++) user_buf[17 + idx*32 + i] = pw[i];
    
    user_buf[0]++; 
    write_sectors(USER_LBA, 1, user_buf); 
    print_string(msg_cmd_ok);
}

// --- KERNEL ENTRY POINT (MAIN LOOP) ---
void kernel_main() {
    clear_screen();
    mouse_init(); // Inisialisasi Mouse & Keyboard USB Legacy Support
    detect_fat_type(); // Deteksi otomatis FAT12 / FAT16 / FAT32
    load_fat();
    load_dir();

    if (curr_cluster == 0 && dir_buf[0] == 0) {
        uint8_t *slot = find_free_slot();
        if (slot) {
            uint32_t cluster = alloc_cluster();
            if (cluster != 0) {
                for (int i = 0; i < 11; i++) slot[i] = "DOCUMENTS  "[i];
                slot[11] = 0x10;
                *((uint16_t*)(slot + 26)) = (uint16_t)cluster;
                save_dir();
            }
        }
    }

    // ================== INIT LOGIN SYSTEM ==================
    read_sectors(USER_LBA, 1, user_buf);
    
    if (user_buf[0] == 0 || user_buf[0] == 0xFF) { 
        print_string("No account found. Please create an admin account.\r\n");
        char un[16], pw[16];
        for(int i=0; i<16; i++) { un[i]=0; pw[i]=0; }
        print_string("Username: "); get_input(un);
        print_string("Password: "); get_password(pw);
        
        user_buf[0] = 1; 
        for(int i=0; i<16; i++) { user_buf[1+i] = 0; user_buf[17+i] = 0; }
        for(int i=0; i<15 && un[i]; i++) user_buf[1+i] = un[i];
        for(int i=0; i<15 && pw[i]; i++) user_buf[17+i] = pw[i];
        
        write_sectors(USER_LBA, 1, user_buf);
        print_string("Account created successfully!\r\n");
    }

    while (true) {
        // ================== LOGIN LOOP ==================
        bool logged_in = false;
        while (!logged_in) {
            char un[16], pw[16];
            for(int i=0; i<16; i++) { un[i]=0; pw[i]=0; }
            print_string("\r\n--- SYSTEM LOGIN ---\r\n");
            print_string("Username: "); get_input(un);
            print_string("Password: "); get_password(pw);
            
            for (int i = 0; i < user_buf[0]; i++) {
                char *s_un = (char*)&user_buf[1 + i*32];
                char *s_pw = (char*)&user_buf[17 + i*32];
                if (str_eq(un, s_un) && str_eq(pw, s_pw)) {
                    logged_in = true;
                    break;
                }
            }
            if (!logged_in) print_string("Invalid credentials!\r\n");
        }

        // ================== OS SHELL ==================
        clear_screen();
        print_string(msg_title);
        
        char input_buffer[64];
        char *arg;

        while (true) {
            print_string(prompt);
            get_input(input_buffer);

            if (match_cmd(input_buffer, "cd", &arg)) do_cd(arg);
            else if (match_cmd(input_buffer, "dir", &arg)) do_dir();
            else if (match_cmd(input_buffer, "mkdir", &arg)) do_mkdir(arg);
            else if (match_cmd(input_buffer, "mkfl", &arg)) do_mkfl(arg);
            else if (match_cmd(input_buffer, "edit", &arg)) do_edit(arg);
            else if (match_cmd(input_buffer, "view", &arg)) do_view(arg);
            else if (match_cmd(input_buffer, "ren", &arg)) do_ren(arg);
            else if (match_cmd(input_buffer, "copy", &arg)) do_copy(arg);
            else if (match_cmd(input_buffer, "move", &arg)) do_move(arg);
            else if (match_cmd(input_buffer, "del", &arg)) do_del(arg);
            else if (match_cmd(input_buffer, "Date", &arg)) do_date();
            else if (match_cmd(input_buffer, "Set Date", &arg)) do_set_date(arg);
            else if (match_cmd(input_buffer, "create account", &arg)) do_create_account();
            else if (match_cmd(input_buffer, "logout", &arg)) { 
                print_string("Logging out...\r\n"); 
                break; 
            }
            else if (match_cmd(input_buffer, "help", &arg)) print_string(msg_help);
            else if (match_cmd(input_buffer, "clear", &arg)) clear_screen();
            else if (match_cmd(input_buffer, "info", &arg)) print_string(msg_info);
            else if (match_cmd(input_buffer, "hardware info", &arg)) do_hw();
            else if (match_cmd(input_buffer, "open Script", &arg)) do_script();
            else if (match_cmd(input_buffer, "exit", &arg)) { 
                print_string(msg_exit); 
                goto system_halt; 
            }
            else if (input_buffer[0] != 0) print_string(msg_error);
        }
    }

system_halt:
    while(1) { __asm__ __volatile__ ("hlt"); } 
}
