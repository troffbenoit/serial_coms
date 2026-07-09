/******************************************************************************
 * FILE:
 *     serial_terminal_ncurses.c
 *
 * DATE:
 *     2026-05-28
 *
 * PURPOSE:
 *     ncurses-based serial terminal for Arduino NanoBee and FPGA UART testing.
 *
 * CHANGE LOG:
 *     2026-05-28:
 *         - Created ncurses version.
 *         - Keeps serial port scan menu.
 *         - Keeps baud/data/parity/stop-bit setup.
 *         - Adds RX display window.
 *         - Adds command entry line.
 *         - Adds status bar.
 *         - Sends clean commands only after ENTER.
 *         - Backspace edits local command buffer only.
 *
 ******************************************************************************/

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

/******************************************************************************
 * PROGRAM LIMITS
 ******************************************************************************/
#define MAX_SERIAL_PORTS        32
#define MAX_DEVICE_PATH_LENGTH  512
#define MAX_COMMAND_LENGTH      80
#define MAX_MENU_LINE_LENGTH    64

/******************************************************************************
 * ASCII CONTROL CODES
 ******************************************************************************/
#define ASCII_BACKSPACE         0x08
#define ASCII_DELETE            0x7F
#define ASCII_CARRIAGE_RETURN   '\r'
#define ASCII_LINE_FEED         '\n'
#define ASCII_CTRL_C            0x03

/******************************************************************************
 * PARITY OPTIONS
 ******************************************************************************/
#define PARITY_NONE             0
#define PARITY_EVEN             1
#define PARITY_ODD              2

/******************************************************************************
 * TYPE:
 *     serial_settings_t
 *
 * PURPOSE:
 *     Holds selected UART settings.
 ******************************************************************************/
typedef struct
{
    speed_t baud_rate_constant;
    int     baud_rate_number;
    int     data_bits;
    int     parity_mode;
    int     stop_bits;
} serial_settings_t;

/******************************************************************************
 * FUNCTION:
 *     read_menu_number
 *
 * PURPOSE:
 *     Read setup menu number before ncurses starts.
 ******************************************************************************/
static int read_menu_number(int default_value)
{
    char menu_line[MAX_MENU_LINE_LENGTH];
    char *end_pointer;
    long parsed_value;

    memset(menu_line, 0, sizeof(menu_line));

    if (fgets(menu_line, sizeof(menu_line), stdin) == NULL)
    {
        return default_value;
    }

    if (menu_line[0] == '\n')
    {
        return default_value;
    }

    parsed_value = strtol(menu_line, &end_pointer, 10);

    if (end_pointer == menu_line)
    {
        return default_value;
    }

    return (int)parsed_value;
}

/******************************************************************************
 * FUNCTION:
 *     device_name_is_serial
 *
 * PURPOSE:
 *     Decide if a /dev entry looks like a USB serial device.
 ******************************************************************************/
static int device_name_is_serial(const char *device_name)
{
    if (device_name == NULL)
    {
        return 0;
    }

#ifdef __APPLE__

    if (strstr(device_name, "Bluetooth") != NULL)
    {
        return 0;
    }

    if (strncmp(device_name, "cu.usbserial", 12) == 0)
    {
        return 1;
    }

    if (strncmp(device_name, "cu.usbmodem", 11) == 0)
    {
        return 1;
    }

    if (strncmp(device_name, "tty.usbserial", 13) == 0)
    {
        return 1;
    }

    if (strncmp(device_name, "tty.usbmodem", 12) == 0)
    {
        return 1;
    }

#elif defined(__linux__)

    if (strncmp(device_name, "ttyUSB", 6) == 0)
    {
        return 1;
    }

    if (strncmp(device_name, "ttyACM", 6) == 0)
    {
        return 1;
    }

#endif

    return 0;
}

/******************************************************************************
 * FUNCTION:
 *     scan_serial_ports
 ******************************************************************************/
static int scan_serial_ports(char ports[MAX_SERIAL_PORTS][MAX_DEVICE_PATH_LENGTH])
{
    DIR *dev_directory;
    struct dirent *entry;
    int port_count;

    port_count = 0;

    dev_directory = opendir("/dev");

    if (dev_directory == NULL)
    {
        perror("Could not open /dev");
        return 0;
    }

    while ((entry = readdir(dev_directory)) != NULL)
    {
        if (device_name_is_serial(entry->d_name) != 0)
        {
            if (port_count < MAX_SERIAL_PORTS)
            {
                snprintf(ports[port_count],
                         MAX_DEVICE_PATH_LENGTH,
                         "/dev/%s",
                         entry->d_name);

                port_count++;
            }
        }
    }

    closedir(dev_directory);

    return port_count;
}

/******************************************************************************
 * FUNCTION:
 *     choose_serial_port
 ******************************************************************************/
static int choose_serial_port(char ports[MAX_SERIAL_PORTS][MAX_DEVICE_PATH_LENGTH],
                              int port_count)
{
    int index;
    int user_choice;

    printf("\nDetected serial ports:\n");
    printf("--------------------------------------------------\n");

    for (index = 0; index < port_count; index++)
    {
        printf("  %d) %s\n", index + 1, ports[index]);
    }

    printf("--------------------------------------------------\n");
    printf("Select serial port number [1]: ");
    fflush(stdout);

    user_choice = read_menu_number(1);

    if ((user_choice < 1) || (user_choice > port_count))
    {
        return -1;
    }

    return user_choice - 1;
}

/******************************************************************************
 * FUNCTION:
 *     choose_baud_rate
 ******************************************************************************/
static void choose_baud_rate(serial_settings_t *settings)
{
    int user_choice;

    printf("\nSelect baud rate:\n");
    printf("--------------------------------------------------\n");
    printf("  1) 9600\n");
    printf("  2) 19200\n");
    printf("  3) 38400\n");
    printf("  4) 57600\n");
    printf("  5) 115200\n");
    printf("--------------------------------------------------\n");
    printf("Select baud rate number [5]: ");
    fflush(stdout);

    user_choice = read_menu_number(5);

    switch (user_choice)
    {
        case 1:
            settings->baud_rate_constant = B9600;
            settings->baud_rate_number = 9600;
            break;

        case 2:
            settings->baud_rate_constant = B19200;
            settings->baud_rate_number = 19200;
            break;

        case 3:
            settings->baud_rate_constant = B38400;
            settings->baud_rate_number = 38400;
            break;

        case 4:
            settings->baud_rate_constant = B57600;
            settings->baud_rate_number = 57600;
            break;

        case 5:
        default:
            settings->baud_rate_constant = B115200;
            settings->baud_rate_number = 115200;
            break;
    }
}

/******************************************************************************
 * FUNCTION:
 *     choose_data_bits
 ******************************************************************************/
static void choose_data_bits(serial_settings_t *settings)
{
    int user_choice;

    printf("\nSelect data bits:\n");
    printf("  1) 7 data bits\n");
    printf("  2) 8 data bits\n");
    printf("Select data bits number [2]: ");
    fflush(stdout);

    user_choice = read_menu_number(2);

    if (user_choice == 1)
    {
        settings->data_bits = 7;
    }
    else
    {
        settings->data_bits = 8;
    }
}

/******************************************************************************
 * FUNCTION:
 *     choose_parity
 ******************************************************************************/
static void choose_parity(serial_settings_t *settings)
{
    int user_choice;

    printf("\nSelect parity:\n");
    printf("  1) None\n");
    printf("  2) Even\n");
    printf("  3) Odd\n");
    printf("Select parity number [1]: ");
    fflush(stdout);

    user_choice = read_menu_number(1);

    if (user_choice == 2)
    {
        settings->parity_mode = PARITY_EVEN;
    }
    else if (user_choice == 3)
    {
        settings->parity_mode = PARITY_ODD;
    }
    else
    {
        settings->parity_mode = PARITY_NONE;
    }
}

/******************************************************************************
 * FUNCTION:
 *     choose_stop_bits
 ******************************************************************************/
static void choose_stop_bits(serial_settings_t *settings)
{
    int user_choice;

    printf("\nSelect stop bits:\n");
    printf("  1) 1 stop bit\n");
    printf("  2) 2 stop bits\n");
    printf("Select stop bits number [1]: ");
    fflush(stdout);

    user_choice = read_menu_number(1);

    if (user_choice == 2)
    {
        settings->stop_bits = 2;
    }
    else
    {
        settings->stop_bits = 1;
    }
}

/******************************************************************************
 * FUNCTION:
 *     parity_name
 ******************************************************************************/
static const char *parity_name(int parity_mode)
{
    if (parity_mode == PARITY_EVEN)
    {
        return "Even";
    }

    if (parity_mode == PARITY_ODD)
    {
        return "Odd";
    }

    return "None";
}

/******************************************************************************
 * FUNCTION:
 *     configure_serial_port
 ******************************************************************************/
static int configure_serial_port(int serial_fd,
                                 const serial_settings_t *settings)
{
    struct termios serial_config;

    memset(&serial_config, 0, sizeof(serial_config));

    if (tcgetattr(serial_fd, &serial_config) != 0)
    {
        perror("tcgetattr serial failed");
        return -1;
    }

    cfsetispeed(&serial_config, settings->baud_rate_constant);
    cfsetospeed(&serial_config, settings->baud_rate_constant);

    serial_config.c_cflag |= (CLOCAL | CREAD);
    serial_config.c_cflag &= ~CSIZE;

    if (settings->data_bits == 7)
    {
        serial_config.c_cflag |= CS7;
    }
    else
    {
        serial_config.c_cflag |= CS8;
    }

    if (settings->parity_mode == PARITY_NONE)
    {
        serial_config.c_cflag &= ~PARENB;
    }
    else if (settings->parity_mode == PARITY_EVEN)
    {
        serial_config.c_cflag |= PARENB;
        serial_config.c_cflag &= ~PARODD;
    }
    else
    {
        serial_config.c_cflag |= PARENB;
        serial_config.c_cflag |= PARODD;
    }

    if (settings->stop_bits == 2)
    {
        serial_config.c_cflag |= CSTOPB;
    }
    else
    {
        serial_config.c_cflag &= ~CSTOPB;
    }

    serial_config.c_cflag &= ~CRTSCTS;

    serial_config.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    serial_config.c_iflag &= ~(IXON | IXOFF | IXANY);
    serial_config.c_iflag &= ~(ICRNL | INLCR);
    serial_config.c_oflag &= ~OPOST;

    serial_config.c_cc[VMIN] = 0;
    serial_config.c_cc[VTIME] = 0;

    if (tcsetattr(serial_fd, TCSANOW, &serial_config) != 0)
    {
        perror("tcsetattr serial failed");
        return -1;
    }

    return 0;
}

/******************************************************************************
 * FUNCTION:
 *     draw_static_screen
 *
 * PURPOSE:
 *     Draw ncurses terminal frame.
 ******************************************************************************/
static void draw_static_screen(WINDOW *rx_window,
                               WINDOW *input_window,
                               const char *port_name,
                               const serial_settings_t *settings)
{
    clear();

    mvprintw(0,
             0,
             " SERIAL TERMINAL NCURSES | Port: %s | %d %d-%s-%d | CTRL-C exits ",
             port_name,
             settings->baud_rate_number,
             settings->data_bits,
             parity_name(settings->parity_mode),
             settings->stop_bits);

    mvhline(1, 0, ACS_HLINE, COLS);

    box(rx_window, 0, 0);
    mvwprintw(rx_window, 0, 2, " RX FROM DEVICE ");
    wrefresh(rx_window);

    box(input_window, 0, 0);
    mvwprintw(input_window, 0, 2, " COMMAND ENTRY ");
    mvwprintw(input_window, 1, 1, "TX> ");
    wrefresh(input_window);

    refresh();
}

/******************************************************************************
 * FUNCTION:
 *     redraw_command_line
 *
 * PURPOSE:
 *     Update command entry window after typing or backspace.
 ******************************************************************************/
static void redraw_command_line(WINDOW *input_window,
                                const char command_buffer[MAX_COMMAND_LENGTH + 1])
{
    werase(input_window);
    box(input_window, 0, 0);
    mvwprintw(input_window, 0, 2, " COMMAND ENTRY ");
    mvwprintw(input_window, 1, 1, "TX> %s", command_buffer);
    wrefresh(input_window);
}

/******************************************************************************
 * FUNCTION:
 *     process_serial_character
 *
 * PURPOSE:
 *     Print incoming device byte into RX window.
 ******************************************************************************/
static void process_serial_character(WINDOW *rx_window,
                                     unsigned char serial_char)
{
    if (serial_char == ASCII_CARRIAGE_RETURN)
    {
        return;
    }

    if (serial_char == ASCII_LINE_FEED)
    {
        waddch(rx_window, '\n');
    }
    else
    {
        waddch(rx_window, serial_char);
    }

    wrefresh(rx_window);
}

/******************************************************************************
 * FUNCTION:
 *     process_keyboard_character
 *
 * PURPOSE:
 *     Edit local command line and transmit only on ENTER.
 ******************************************************************************/
static int process_keyboard_character(int serial_fd,
                                      WINDOW *input_window,
                                      int keyboard_char,
                                      char command_buffer[MAX_COMMAND_LENGTH + 1],
                                      size_t *command_length)
{
    unsigned char carriage_return;

    if (keyboard_char == ASCII_CTRL_C)
    {
        return 1;
    }

    if ((keyboard_char == KEY_BACKSPACE) ||
        (keyboard_char == ASCII_BACKSPACE) ||
        (keyboard_char == ASCII_DELETE))
    {
        if (*command_length > 0)
        {
            *command_length = *command_length - 1;
            command_buffer[*command_length] = '\0';
            redraw_command_line(input_window, command_buffer);
        }

        return 0;
    }

    if ((keyboard_char == ASCII_CARRIAGE_RETURN) ||
        (keyboard_char == ASCII_LINE_FEED) ||
        (keyboard_char == '\n'))
    {
        if (*command_length > 0)
        {
            write(serial_fd, command_buffer, *command_length);
        }

        carriage_return = ASCII_CARRIAGE_RETURN;
        write(serial_fd, &carriage_return, 1);

        memset(command_buffer, 0, MAX_COMMAND_LENGTH + 1);
        *command_length = 0;

        redraw_command_line(input_window, command_buffer);

        return 0;
    }

    if ((keyboard_char >= 32) && (keyboard_char <= 126))
    {
        if (*command_length < MAX_COMMAND_LENGTH)
        {
            command_buffer[*command_length] = (char)keyboard_char;
            *command_length = *command_length + 1;
            command_buffer[*command_length] = '\0';
            redraw_command_line(input_window, command_buffer);
        }
    }

    return 0;
}

/******************************************************************************
 * FUNCTION:
 *     main
 ******************************************************************************/
int main(void)
{
    char serial_ports[MAX_SERIAL_PORTS][MAX_DEVICE_PATH_LENGTH];
    char command_buffer[MAX_COMMAND_LENGTH + 1];

    int port_count;
    int selected_port_index;
    int serial_fd;
    int exit_requested;
    int keyboard_char;
    int highest_fd;
    int select_result;

    unsigned char serial_char;

    ssize_t bytes_read;

    size_t command_length;

    fd_set read_fd_set;

    WINDOW *rx_window;
    WINDOW *input_window;

    serial_settings_t settings;

    memset(serial_ports, 0, sizeof(serial_ports));
    memset(command_buffer, 0, sizeof(command_buffer));
    memset(&settings, 0, sizeof(settings));

    command_length = 0;
    exit_requested = 0;

    settings.baud_rate_constant = B115200;
    settings.baud_rate_number = 115200;
    settings.data_bits = 8;
    settings.parity_mode = PARITY_NONE;
    settings.stop_bits = 1;

    port_count = scan_serial_ports(serial_ports);

    if (port_count <= 0)
    {
        printf("No usable USB serial ports found.\n");
        printf("Try:\n");

#ifdef __APPLE__
        printf("    ls /dev/cu.*\n");
#elif defined(__linux__)
        printf("    ls /dev/ttyACM*\n");
        printf("    ls /dev/ttyUSB*\n");
#else
        printf("    Check your system serial device list.\n");
#endif

        return EXIT_FAILURE;
    }

    selected_port_index = choose_serial_port(serial_ports, port_count);

    if (selected_port_index < 0)
    {
        printf("Invalid serial port selection.\n");
        return EXIT_FAILURE;
    }

    choose_baud_rate(&settings);
    choose_data_bits(&settings);
    choose_parity(&settings);
    choose_stop_bits(&settings);

    serial_fd = open(serial_ports[selected_port_index],
                     O_RDWR | O_NOCTTY | O_NONBLOCK);

    if (serial_fd < 0)
    {
        perror("Could not open selected serial device");
        return EXIT_FAILURE;
    }

    if (configure_serial_port(serial_fd, &settings) != 0)
    {
        close(serial_fd);
        return EXIT_FAILURE;
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);

    rx_window = newwin(LINES - 5, COLS, 2, 0);
    input_window = newwin(3, COLS, LINES - 3, 0);

    scrollok(rx_window, TRUE);
    keypad(input_window, TRUE);
    nodelay(input_window, TRUE);

    draw_static_screen(rx_window,
                       input_window,
                       serial_ports[selected_port_index],
                       &settings);

    while (exit_requested == 0)
    {
        FD_ZERO(&read_fd_set);
        FD_SET(STDIN_FILENO, &read_fd_set);
        FD_SET(serial_fd, &read_fd_set);

        highest_fd = serial_fd;

        if (STDIN_FILENO > highest_fd)
        {
            highest_fd = STDIN_FILENO;
        }

        select_result = select(highest_fd + 1,
                               &read_fd_set,
                               NULL,
                               NULL,
                               NULL);

        if (select_result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            break;
        }

        if (FD_ISSET(STDIN_FILENO, &read_fd_set))
        {
            keyboard_char = wgetch(input_window);

            if (keyboard_char != ERR)
            {
                exit_requested =
                    process_keyboard_character(serial_fd,
                                               input_window,
                                               keyboard_char,
                                               command_buffer,
                                               &command_length);
            }
        }

        if (FD_ISSET(serial_fd, &read_fd_set))
        {
            bytes_read = read(serial_fd, &serial_char, 1);

            while (bytes_read > 0)
            {
                process_serial_character(rx_window, serial_char);
                bytes_read = read(serial_fd, &serial_char, 1);
            }
        }
    }

    delwin(rx_window);
    delwin(input_window);
    endwin();

    close(serial_fd);

    printf("Serial terminal closed cleanly.\n");

    return EXIT_SUCCESS;
}

