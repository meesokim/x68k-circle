#include <stdint.h>
#include <stddef.h>
#include <string.h>

// _sbrk implementation for newlib heap
void *_sbrk(ptrdiff_t incr) {
    extern char _end;
    static char *heap_end = 0;
    char *prev_heap_end;

    if (heap_end == 0) {
        heap_end = &_end;
    }
    prev_heap_end = heap_end;
    heap_end += incr;

    return (void *)prev_heap_end;
}

// strlcpy implementation for libretro
size_t strlcpy_retro__(char *dest, const char *src, size_t size)
{
    size_t src_len = strlen(src);
    if (size > 0) {
        size_t copy_len = (src_len >= size) ? size - 1 : src_len;
        memcpy(dest, src, copy_len);
        dest[copy_len] = '\0';
    }
    return src_len;
}

// Stubs for webx68k hooks added to px68k-libretro fork
void webx68k_fdd_insert(int drive, const char *path) { (void)drive; (void)path; }
void webx68k_fdd_eject(int drive) { (void)drive; }
int webx68k_joystick_read(int port) { (void)port; return 0; }
int webx68k_peek16(unsigned int addr) { (void)addr; return 0; }
int webx68k_peek8(unsigned int addr) { (void)addr; return 0; }
void webx68k_mem_read(unsigned int addr, unsigned char *buf, int len) { (void)addr; (void)buf; (void)len; }
void webx68k_mem_write(unsigned int addr, const unsigned char *buf, int len) { (void)addr; (void)buf; (void)len; }
int webx68k_hostfs_request(unsigned int addr) { (void)addr; return 0; }
int webx68k_hostfs_poll(void) { return 0; }
int webx68k_keybuf_peek(unsigned int index) { (void)index; return -1; }
int webx68k_keybuf_write_pointer(void) { return 0; }
int webx68k_text_dot_x(void) { return 0; }
int webx68k_text_dot_y(void) { return 0; }
int webx68k_text_scroll_x(void) { return 0; }
int webx68k_text_scroll_y(void) { return 0; }
int webx68k_sram_read(int offset) { (void)offset; return 0; }
void webx68k_send_key_make(int scancode) { (void)scancode; }

int webx68k_scsi_get_size(void) { return 0; }
int webx68k_scsi_read_sector(unsigned int lba, unsigned char *buf) { (void)lba; (void)buf; return -1; }
int webx68k_scsi_write_sector(unsigned int lba, const unsigned char *buf) { (void)lba; (void)buf; return -1; }
int webx68k_scsi_init_d2(void) { return 0; }
int webx68k_scsi_init_a4(void) { return 0x00ea0100; }
int webx68k_scsi_drv_attr(void) { return 0x4000; }
int webx68k_scsi_sram_init(void) { return 0; }
int webx68k_scsi_reply_err(void) { return 0; }
int webx68k_scsi_reply_units(void) { return 1; }
int webx68k_scsi_reply_init_once(void) { return 1; }
int webx68k_scsi_reply_end(void) { return 0x00ea0500; }
int webx68k_scsi_reply_bpb(void) { return 0x00ea0600; }
int webx68k_scsi_reply_status(void) { return -1; }
int webx68k_scsi_reply_d0(void) { return -1; }
int webx68k_scsi_req_status_off(void) { return -1; }
int webx68k_scsi_req_status_val(void) { return 0; }
int webx68k_scsi_cmd_answer_cmd(void) { return -1; }
int webx68k_scsi_cmd_answer_off(void) { return -1; }
int webx68k_scsi_cmd_answer_val(void) { return 0; }
int webx68k_scsi_cmd_fill_cmd(void) { return -1; }
int webx68k_scsi_cmd_fill_lo(void) { return -1; }
int webx68k_scsi_cmd_fill_hi(void) { return -1; }
int webx68k_scsi_cmd_fill_val(void) { return 0; }
int webx68k_scsi_oracle_reply(void) { return 0; }
int webx68k_scsi_verbose_log(void) { return 0; }
int webx68k_scsi_rom_len(void) { return 0; }
int webx68k_scsi_rom_byte(int i) { (void)i; return 0; }
unsigned int webx68k_scsi_drv_next(void) { return 0; }
unsigned int webx68k_scsi_drv_ram(void) { return 0; }
unsigned int webx68k_scsi_drv_ram_from(void) { return 0; }
unsigned int webx68k_scsi_rom_header_end(void) { return 0; }

int webx68k_scsi_spc_ints_sel(void) { return 0x08; }
int webx68k_scsi_spc_ints_timeout(void) { return 0x20; }
int webx68k_scsi_spc_target(void) { return -1; }
int webx68k_scsi_spc_ssts(void) { return -1; }
int webx68k_scsi_spc_psns(void) { return -1; }
int webx68k_scsi_spc_psns_a(void) { return 0x8a; }
int webx68k_scsi_spc_psns_b(void) { return 0x0a; }
int webx68k_scsi_spc_psns_base(void) { return 0; }
int webx68k_scsi_spc_ssts_base(void) { return 0; }
int webx68k_scsi_spc_clear_on_pctl(void) { return 1; }
int webx68k_scsi_spc_phase_bits(void) { return 0; }
int webx68k_scsi_spc_ints_xfer(void) { return 0; }
int webx68k_scsi_spc_ints_disc(void) { return 0; }
int webx68k_scsi_spc_cdb_from_temp(void) { return 1; }
int webx68k_scsi_spc_ssts_data_bit(void) { return 0x08; }
int webx68k_scsi_spc_ssts_tc0_bit(void) { return 0x10; }
int webx68k_scsi_bus_pc_limit(void) { return 0; }
int webx68k_scsi_bus_log_max(void) { return 0; }

void webx68k_ram_watch_refresh(void) {}
void webx68k_mem_read_watch_refresh(void) {}
void webx68k_drv_hook_refresh(void) {}
void webx68k_trace_start(const char *tag) { (void)tag; }

int webx68k_serial_rx(const uint8_t *data, int length) { (void)data; (void)length; return 0; }
int webx68k_serial_tx_available(void) { return 0; }
int webx68k_serial_tx_drain(uint8_t *data, int max_length) { (void)data; (void)max_length; return 0; }
void webx68k_serial_reset(void) {}
void webx68k_serial_set_connected(int connected) { (void)connected; }
void webx68k_serial_set_tx_writable(int writable) { (void)writable; }
int webx68k_serial_guest_baud_rate(void) { return 9600; }

int webx68k_scc_read(unsigned int address) { (void)address; return 0; }
void webx68k_scc_write(unsigned int address, int value) { (void)address; (void)value; }
int webx68k_scc_test_acknowledge_irq(void) { return 0; }
int webx68k_scc_test_interrupt_cause(void) { return 0; }
int webx68k_scc_test_irq_pending(void) { return 0; }
