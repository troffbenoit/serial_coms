#include <ncurses.h>

int main(void)
{
    initscr();
    printw("ncurses works\n");
    refresh();
    getch();
    endwin();
    return 0;
}
