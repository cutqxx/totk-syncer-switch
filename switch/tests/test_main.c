#include <stdio.h>

#include "test.h"

int ts_checks = 0;
int ts_failures = 0;

void run_api_tests(void);
void run_body_tests(void);
void run_config_tests(void);
void run_http_tests(void);
void run_ipv4_tests(void);
void run_log_tests(void);
void run_main_screen_tests(void);
void run_pc_status_tests(void);
void run_redact_tests(void);
void run_retry_tests(void);
void run_text_and_repo_rule_tests(void);

int main(void)
{
    run_ipv4_tests();
    run_config_tests();
    run_http_tests();
    run_body_tests();
    run_retry_tests();
    run_api_tests();
    run_pc_status_tests();
    run_redact_tests();
    run_log_tests();
    run_main_screen_tests();
    run_text_and_repo_rule_tests();
    printf("%d checks, %d failures\n", ts_checks, ts_failures);
    return ts_failures == 0 ? 0 : 1;
}
