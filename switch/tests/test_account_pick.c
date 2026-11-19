#include <stdio.h>
#include <string.h>

#include "account_pick.h"
#include "test.h"
#include "test_support.h"
#include "text.h"

static const uint64_t UID[2] = { 0xAAAA, 0xBBBB };

static void set_accounts(fake_console_t *console, int count)
{
    console->account_count = count;
    for (int i = 0; i < count; i++) {
        char nickname[32];
        uint64_t uid[2] = { (uint64_t)i + 1, 0 };
        snprintf(nickname, sizeof nickname, "Player%d", i);
        account_make(&console->accounts[i], uid, nickname, i + 1);
    }
}

/* ---- account_make ---- */

static void an_ascii_nickname_is_kept_as_it_is(void)
{
    account_t account;
    account_make(&account, UID, "Serezha_01", 1);
    CHECK_STR_EQ(account.name, "Serezha_01");
    CHECK_INT_EQ(account.uid[0], 0xAAAA);
    CHECK_INT_EQ(account.uid[1], 0xBBBB);
}

static void every_non_ascii_byte_of_the_nickname_becomes_a_question_mark(void)
{
    account_t account;
    account_make(&account, UID, "\xD0\xA1\xD0\xB5\xD1\x80" "a", 2);
    CHECK_STR_EQ(account.name, "??????a");
}

static void control_characters_and_delete_in_the_nickname_become_question_marks(void)
{
    account_t account;
    account_make(&account, UID, "a\tb\x7f" "c\x01", 1);
    CHECK_STR_EQ(account.name, "a?b?c?");
}

static void an_empty_or_missing_nickname_becomes_account_and_its_number(void)
{
    account_t account;
    account_make(&account, UID, "", 3);
    CHECK_STR_EQ(account.name, "Account 3");
    account_make(&account, UID, NULL, 1);
    CHECK_STR_EQ(account.name, "Account 1");
}

static void a_nickname_longer_than_32_characters_is_cut_and_terminated(void)
{
    account_t account;
    account_make(&account, UID, "0123456789012345678901234567890123456789", 1);
    CHECK_STR_EQ(account.name, "01234567890123456789012345678901");
    CHECK_INT_EQ(strlen(account.name), 32);
}

static void a_nickname_of_exactly_32_characters_is_kept_whole(void)
{
    account_t account;
    account_make(&account, UID, "abcdefghijklmnopqrstuvwxyz012345", 1);
    CHECK_STR_EQ(account.name, "abcdefghijklmnopqrstuvwxyz012345");
}

/* ---- account_pick ---- */

static void no_account_with_a_save_gives_no_save_and_asks_nobody(void)
{
    fake_world_t world;
    fake_console_t console;
    account_t chosen;
    fake_world_init(&world, NULL, 0);
    fake_console_init(&console, &world);
    console.account_count = 0;
    CHECK_INT_EQ(account_pick(&world.env, "Pick one", &chosen), PICK_NO_SAVE);
    CHECK_INT_EQ(world.choose_calls, 0);
}

static void a_single_account_is_chosen_without_asking(void)
{
    fake_world_t world;
    fake_console_t console;
    account_t chosen;
    fake_world_init(&world, NULL, 0);
    fake_console_init(&console, &world);
    CHECK_INT_EQ(account_pick(&world.env, "Pick one", &chosen), PICK_CHOSEN);
    CHECK_STR_EQ(chosen.name, "Player");
    CHECK_INT_EQ(chosen.uid[0], 0x1111);
    CHECK_INT_EQ(world.choose_calls, 0);
}

static void several_accounts_are_offered_and_the_picked_one_is_returned(void)
{
    fake_world_t world;
    fake_console_t console;
    account_t chosen;
    fake_world_init(&world, NULL, 0);
    fake_console_init(&console, &world);
    set_accounts(&console, 3);
    world.choose_result = 2;
    CHECK_INT_EQ(account_pick(&world.env, "Pick one", &chosen), PICK_CHOSEN);
    CHECK_INT_EQ(world.choose_calls, 1);
    CHECK_INT_EQ(world.choose_offered, 3);
    CHECK_STR_EQ(chosen.name, "Player2");
    CHECK_INT_EQ(chosen.uid[0], 3);
}

static void backing_out_of_the_list_cancels(void)
{
    fake_world_t world;
    fake_console_t console;
    account_t chosen;
    fake_world_init(&world, NULL, 0);
    fake_console_init(&console, &world);
    set_accounts(&console, 2);
    world.choose_result = -1;
    CHECK_INT_EQ(account_pick(&world.env, "Pick one", &chosen), PICK_CANCELLED);
    CHECK_INT_EQ(world.choose_calls, 1);
}

static void an_index_outside_the_list_counts_as_cancelled(void)
{
    static const int indexes[] = { 2, 3, 100, -2 };
    for (size_t i = 0; i < sizeof indexes / sizeof indexes[0]; i++) {
        fake_world_t world;
        fake_console_t console;
        account_t chosen;
        fake_world_init(&world, NULL, 0);
        fake_console_init(&console, &world);
        set_accounts(&console, 2);
        world.choose_result = indexes[i];
        CHECK_INT_EQ(account_pick(&world.env, "Pick one", &chosen), PICK_CANCELLED);
    }
}

static void at_most_eight_accounts_are_offered(void)
{
    fake_world_t world;
    fake_console_t console;
    account_t chosen;
    fake_world_init(&world, NULL, 0);
    fake_console_init(&console, &world);
    set_accounts(&console, ACCOUNT_MAX);
    world.choose_result = ACCOUNT_MAX - 1;
    CHECK_INT_EQ(account_pick(&world.env, "Pick one", &chosen), PICK_CHOSEN);
    CHECK_INT_EQ(world.choose_offered, ACCOUNT_MAX);
    CHECK_STR_EQ(chosen.name, "Player7");
}

/* ---- account_menu_render ---- */

static void the_menu_marks_only_the_selected_account(void)
{
    account_t list[3];
    char text[512];
    uint64_t uid[2] = { 1, 2 };
    account_make(&list[0], uid, "Alice", 1);
    account_make(&list[1], uid, "Bob", 2);
    account_make(&list[2], uid, "Carol", 3);
    account_menu_render(TXT_ACCOUNT_MENU_TITLE, list, 3, 1, text, sizeof text);
    CHECK_STR_EQ(text,
                 "Choose the account whose TotK save to send\n\n"
                 "  Alice\n"
                 "> Bob\n"
                 "  Carol\n"
                 "\nUp/Down - select    A - choose    B - cancel\n");
}

static void the_menu_with_nothing_selected_marks_nobody(void)
{
    account_t list[2];
    char text[512];
    uint64_t uid[2] = { 1, 2 };
    account_make(&list[0], uid, "Alice", 1);
    account_make(&list[1], uid, "Bob", 2);
    account_menu_render(TXT_ACCOUNT_MENU_TITLE, list, 2, -1, text, sizeof text);
    CHECK_STR_NOT_CONTAINS(text, ">");
}

static void the_worst_case_menu_fits_the_console_buffer_with_its_buttons(void)
{
    account_t list[ACCOUNT_MAX];
    char text[512];
    uint64_t uid[2] = { 1, 2 };
    for (int i = 0; i < ACCOUNT_MAX; i++) {
        account_make(&list[i], uid, "abcdefghijklmnopqrstuvwxyz012345", i + 1);
    }
    account_menu_render(TXT_ACCOUNT_MENU_TITLE, list, ACCOUNT_MAX, ACCOUNT_MAX - 1, text, sizeof text);
    CHECK(strstr(text, "B - cancel\n") != NULL);
    CHECK(strstr(text, "> abcdefghijklmnopqrstuvwxyz012345\n") != NULL);
}

static void the_menu_is_ascii_even_for_a_cyrillic_nickname(void)
{
    account_t list[2];
    char text[512];
    uint64_t uid[2] = { 1, 2 };
    account_make(&list[0], uid, "\xD0\xA1\xD0\xB5\xD1\x80\xD1\x91\xD0\xB6\xD0\xB0", 1);
    account_make(&list[1], uid, "Bob", 2);
    account_menu_render(TXT_ACCOUNT_MENU_TITLE, list, 2, 0, text, sizeof text);
    CHECK(test_is_ascii_text(text));
    CHECK(strstr(text, "> ????????????\n") != NULL);
}

static void a_small_buffer_is_never_overrun_by_the_worst_case_menu(void)
{
    account_t list[ACCOUNT_MAX];
    uint64_t uid[2] = { 1, 2 };
    for (int i = 0; i < ACCOUNT_MAX; i++) {
        account_make(&list[i], uid, "abcdefghijklmnopqrstuvwxyz012345", i + 1);
    }
    for (size_t size = 1; size < 480; size++) {
        char guarded[512];
        memset(guarded, 0x5A, sizeof guarded);
        account_menu_render(TXT_ACCOUNT_MENU_TITLE, list, ACCOUNT_MAX, 3, guarded, size);
        CHECK(memchr(guarded, '\0', size) != NULL);
        CHECK_INT_EQ((unsigned char)guarded[size], 0x5A);
        CHECK_INT_EQ((unsigned char)guarded[size + 8], 0x5A);
    }
}

void run_account_pick_tests(void)
{
    RUN_TEST(an_ascii_nickname_is_kept_as_it_is);
    RUN_TEST(every_non_ascii_byte_of_the_nickname_becomes_a_question_mark);
    RUN_TEST(control_characters_and_delete_in_the_nickname_become_question_marks);
    RUN_TEST(an_empty_or_missing_nickname_becomes_account_and_its_number);
    RUN_TEST(a_nickname_longer_than_32_characters_is_cut_and_terminated);
    RUN_TEST(a_nickname_of_exactly_32_characters_is_kept_whole);
    RUN_TEST(no_account_with_a_save_gives_no_save_and_asks_nobody);
    RUN_TEST(a_single_account_is_chosen_without_asking);
    RUN_TEST(several_accounts_are_offered_and_the_picked_one_is_returned);
    RUN_TEST(backing_out_of_the_list_cancels);
    RUN_TEST(an_index_outside_the_list_counts_as_cancelled);
    RUN_TEST(at_most_eight_accounts_are_offered);
    RUN_TEST(the_menu_marks_only_the_selected_account);
    RUN_TEST(the_menu_with_nothing_selected_marks_nobody);
    RUN_TEST(the_worst_case_menu_fits_the_console_buffer_with_its_buttons);
    RUN_TEST(the_menu_is_ascii_even_for_a_cyrillic_nickname);
    RUN_TEST(a_small_buffer_is_never_overrun_by_the_worst_case_menu);
}
