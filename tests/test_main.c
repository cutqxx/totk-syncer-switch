#include <stdio.h>

#include "test.h"

int ts_checks = 0;
int ts_failures = 0;

void run_url_tests(void);
void run_request_tests(void);
void run_outcome_tests(void);
void run_body_tests(void);
void run_session_tests(void);
void run_config_tests(void);
void run_experiment_tests(void);
void run_redact_tests(void);
void run_log_tests(void);
void run_ui_text_tests(void);

int main(void)
{
    run_url_tests();
    run_request_tests();
    run_outcome_tests();
    run_body_tests();
    run_session_tests();
    run_config_tests();
    run_experiment_tests();
    run_redact_tests();
    run_log_tests();
    run_ui_text_tests();
    printf("%d checks, %d failures\n", ts_checks, ts_failures);
    return ts_failures == 0 ? 0 : 1;
}
