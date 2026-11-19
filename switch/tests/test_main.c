#include <stdio.h>

#include "test.h"

int ts_checks = 0;
int ts_failures = 0;

void run_account_pick_tests(void);
void run_api_put_tests(void);
void run_api_tests(void);
void run_ascii_text_tests(void);
void run_body_tests(void);
void run_config_tests(void);
void run_game_guard_tests(void);
void run_http_tests(void);
void run_ipv4_tests(void);
void run_log_tests(void);
void run_main_screen_tests(void);
void run_pc_status_tests(void);
void run_redact_tests(void);
void run_retry_tests(void);
void run_save_pack_tests(void);
void run_save_rules_tests(void);
void run_save_validate_tests(void);
void run_sd_backup_tests(void);
void run_save_write_tests(void);
void run_api_save_tests(void);
void run_get_from_pc_tests(void);
void run_send_conflict_tests(void);
void run_send_to_pc_tests(void);
void run_text_and_repo_rule_tests(void);
void run_zip_writer_tests(void);

int main(void)
{
    run_ipv4_tests();
    run_config_tests();
    run_http_tests();
    run_body_tests();
    run_retry_tests();
    run_api_tests();
    run_ascii_text_tests();
    run_pc_status_tests();
    run_redact_tests();
    run_log_tests();
    run_main_screen_tests();
    run_save_rules_tests();
    run_save_validate_tests();
    run_sd_backup_tests();
    run_save_write_tests();
    run_api_save_tests();
    run_get_from_pc_tests();
    run_game_guard_tests();
    run_account_pick_tests();
    run_api_put_tests();
    run_zip_writer_tests();
    run_save_pack_tests();
    run_send_to_pc_tests();
    run_send_conflict_tests();
    run_text_and_repo_rule_tests();
    printf("%d checks, %d failures\n", ts_checks, ts_failures);
    return ts_failures == 0 ? 0 : 1;
}
