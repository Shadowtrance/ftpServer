#ifndef FTP_UI_SCREEN_H
#define FTP_UI_SCREEN_H

// Main screen creation
void create_screen_ftp(void);

// Log functions
void addLog(const char* message);
void clearLog(void);

// Update functions
void update_ip_label(const char* ip);
void update_status(const char* status);
void update_time_label(void);
void start_time_update_timer(void);

void register_ftp_control_callback(void (*callback)(bool start));
void set_server_switch_state(bool enabled);
void reset_ftp_operation_flag(void);

#endif // FTP_UI_SCREEN_H
