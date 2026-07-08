/******************************************************************************
 * FILE:
 *     serial_terminal.c
 *
 * DATE:
 *     2026-05-28
 *
 * PURPOSE:
 *     Interactive serial terminal for FPGA UART command testing.
 *
 * FLIGHT-READY DESIGN GOALS:
 *     1. Present available serial ports to the user.
 *     2. Let the user choose UART settings.
 *     3. Display incoming FPGA text like a simple old terminal.
 *     4. Locally edit typed commands before sending them.
 *     5. Prevent Backspace/Delete bytes from corrupting FPGA commands.
 *     6. Send only clean completed command lines to the FPGA.
 *     7. Restore the keyboard cleanly on exit.
 *
 ******************************************************************************/

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/******************************************************************************
 * PROGRAM LIMITS
 ******************************************************************************/
#define MAX_SERIAL_PORTS        32
#define MAX_DEVICE_PATH_LENGTH  256
#define MAX_COMMAND_LENGTH      80

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
 *     Stores the user-selected UART configuration.
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
 *     device_name_is_serial
 *
 * PURPOSE:
 *     Decide whether a /dev entry looks like a usable USB serial device.
 ******************************************************************************/
static int device_name_is_serial(const char *device_name)
{
    if (device_name == NULL)
    {
        return 0;
    }

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

    return 0;
}

/******************************************************************************
 * FUNCTION:
 *     scan_serial_ports
 *
 * PURPOSE:
 *     Scan /dev and collect usable serial port paths.
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
 *
 * PURPOSE:
 *     Present detected serial ports and return selected index.
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
    printf("Select serial port number: ");
    fflush(stdout);

    if (scanf("%d", &user_choice) != 1)
    {
        return -1;
    }

    if ((user_choice < 1) || (user_choice > port_count))
    {
        return -1;
    }

    return user_choice - 1;
}

/******************************************************************************
 * FUNCTION:
 *     choose_baud_rate
 *
 * PURPOSE:
 *     Let user select UART baud rate.
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

    if (scanf("%d", &user_choice) != 1)
    {
        user_choice = 5;
    }

    switch (user_choice)
    {
        case 1:
            settings->baud_rate_constant = B9600;
            settings->baud_rate_number   = 9600;
            break;

        case 2:
            settings->baud_rate_constant = B19200;
            settings->baud_rate_number   = 19200;
            break;

        case 3:
            settings->baud_rate_constant = B38400;
            settings->baud_rate_number   = 38400;
            break;

        case 4:
            settings->baud_rate_constant = B57600;
            settings->baud_rate_number   = 57600;
            break;

        case 5:
        default:
            settings->baud_rate_constant = B115200;
            settings->baud_rate_number   = 115200;
            break;
    }
}

/******************************************************************************
 * FUNCTION:
 *     choose_data_bits
 *
 * PURPOSE:
 *     Let user select serial data width.
 ******************************************************************************/
static void choose_data_bits(serial_settings_t *settings)
{
    int user_choice;

    printf("\nSelect data bits:\n");
    printf("--------------------------------------------------\n");
    printf("  1) 7 data bits\n");
    printf("  2) 8 data bits\n");
    printf("--------------------------------------------------\n");
    printf("Select data bits number [2]: ");
    fflush(stdout);

    if (scanf("%d", &user_choice) != 1)
    {
        user_choice = 2;
    }

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
 *
 * PURPOSE:
 *     Let user select parity mode.
 ******************************************************************************/
static void choose_parity(serial_settings_t *settings)
{
    int user_choice;

    printf("\nSelect parity:\n");
    printf("--------------------------------------------------\n");
    printf("  1) None\n");
    printf("  2) Even\n");
    printf("  3) Odd\n");
    printf("--------------------------------------------------\n");
    printf("Select parity number [1]: ");
    fflush(stdout);

    if (scanf("%d", &user_choice) != 1)
    {
        user_choice = 1;
    }

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
 *
 * PURPOSE:
 *     Let user select one or two stop bits.
 ******************************************************************************/
static void choose_stop_bits(serial_settings_t *settings)
{
    int user_choice;

    printf("\nSelect stop bits:\n");
    printf("--------------------------------------------------\n");
    printf("  1) 1 stop bit\n");
    printf("  2) 2 stop bits\n");
    printf("--------------------------------------------------\n");
    printf("Select stop bits number [1]: ");
    fflush(stdout);

    if (scanf("%d", &user_choice) != 1)
    {
        user_choice = 1;
    }

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
 *     configure_serial_port
 *
 * PURPOSE:
 *     Apply selected UART configuration to the opened serial port.
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

    serial_config.c_cc[VMIN]  = 0;
    serial_config.c_cc[VTIME] = 1;

    if (tcsetattr(serial_fd, TCSANOW, &serial_config) != 0)
    {
        perror("tcsetattr serial failed");
        return -1;
    }

    return 0;
}

/******************************************************************************
 * FUNCTION:
 *     configure_keyboard_raw_mode
 *
 * PURPOSE:
 *     Put the local keyboard into raw mode after setup is complete.
 ******************************************************************************/
static int configure_keyboard_raw_mode(struct termios *old_keyboard_config)
{
    struct termios new_keyboard_config;

    if (tcgetattr(STDIN_FILENO, old_keyboard_config) != 0)
    {
        perror("tcgetattr keyboard failed");
        return -1;
    }

    new_keyboard_config = *old_keyboard_config;

    new_keyboard_config.c_lflag &= ~(ICANON | ECHO);
    new_keyboard_config.c_cc[VMIN]  = 0;
    new_keyboard_config.c_cc[VTIME] = 2;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_keyboard_config) != 0)
    {
        perror("tcsetattr keyboard failed");
        return -1;
    }

    return 0;
}

/******************************************************************************
 * FUNCTION:
 *     parity_name
 *
 * PURPOSE:
 *     Convert parity setting to printable text.
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
 *     process_keyboard_character
 *
 * PURPOSE:
 *     Locally edit a command line and send only clean completed commands.
 *
 * CRITICAL SAFETY RULE:
 *     Backspace/Delete edits the local command buffer only.
 *     Backspace/Delete is never sent to the FPGA.
 ******************************************************************************/
static int process_keyboard_character(int serial_fd,
                                      unsigned char keyboard_char,
                                      char command_buffer[MAX_COMMAND_LENGTH + 1],
                                      size_t *command_length)
{
    if (keyboard_char == ASCII_CTRL_C)
    {
        return 1;
    }

    if ((keyboard_char == ASCII_BACKSPACE) ||
        (keyboard_char == ASCII_DELETE))
    {
        if (*command_length > 0)
        {
            *command_length = *command_length - 1;
            command_buffer[*command_length] = '\0';
            write(STDOUT_FILENO, "\b \b", 3);
        }

        return 0;
    }

    if ((keyboard_char == ASCII_CARRIAGE_RETURN) ||
        (keyboard_char == ASCII_LINE_FEED))
    {
        write(STDOUT_FILENO, "\r\n", 2);

        if (*command_length > 0)
        {
            write(serial_fd, command_buffer, *command_length);
        }

        keyboard_char = ASCII_CARRIAGE_RETURN;
        write(serial_fd, &keyboard_char, 1);

        memset(command_buffer, 0, MAX_COMMAND_LENGTH + 1);
        *command_length = 0;

        return 0;
    }

    if (*command_length < MAX_COMMAND_LENGTH)
    {
        command_buffer[*command_length] = (char)keyboard_char;
        *command_length = *command_length + 1;
        command_buffer[*command_length] = '\0';

        write(STDOUT_FILENO, &keyboard_char, 1);
    }

    return 0;
}

/******************************************************************************
 * FUNCTION:
 *     process_serial_character
 *
 * PURPOSE:
 *     Display incoming serial text like a simple old terminal.
 ******************************************************************************/
static void process_serial_character(unsigned char serial_char)
{
    if (serial_char == ASCII_LINE_FEED)
    {
        write(STDOUT_FILENO, "\r\n", 2);
    }
    else
    {
        write(STDOUT_FILENO, &serial_char, 1);
    }
}

/******************************************************************************
 * FUNCTION:
 *     main
 *
 * PURPOSE:
 *     Program entry point.
 ******************************************************************************/
int main(void)
{
    char serial_ports[MAX_SERIAL_PORTS][MAX_DEVICE_PATH_LENGTH];
    char command_buffer[MAX_COMMAND_LENGTH + 1];

    int port_count;
    int selected_port_index;
    int serial_fd;
    int exit_requested;

    unsigned char keyboard_char;
    unsigned char serial_char;

    ssize_t bytes_read;

    size_t command_length;

    struct termios old_keyboard_config;

    serial_settings_t settings;

    memset(serial_ports, 0, sizeof(serial_ports));
    memset(command_buffer, 0, sizeof(command_buffer));
    memset(&old_keyboard_config, 0, sizeof(old_keyboard_config));
    memset(&settings, 0, sizeof(settings));

    command_length = 0;
    exit_requested = 0;

    settings.baud_rate_constant = B115200;
    settings.baud_rate_number   = 115200;
    settings.data_bits          = 8;
    settings.parity_mode        = PARITY_NONE;
    settings.stop_bits          = 1;

    port_count = scan_serial_ports(serial_ports);

    if (port_count <= 0)
    {
        printf("No usable USB serial ports found.\n");
        printf("Try:\n");
        printf("    ls /dev/cu.*\n");
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

    if (configure_keyboard_raw_mode(&old_keyboard_config) != 0)
    {
        close(serial_fd);
        return EXIT_FAILURE;
    }

    printf("\r\n");
    printf("========================================\r\n");
    printf(" FPGA SERIAL TERMINAL STARTED\r\n");
    printf("========================================\r\n");
    printf(" Port      : %s\r\n", serial_ports[selected_port_index]);
    printf(" Baud      : %d\r\n", settings.baud_rate_number);
    printf(" Data Bits : %d\r\n", settings.data_bits);
    printf(" Parity    : %s\r\n", parity_name(settings.parity_mode));
    printf(" Stop Bits : %d\r\n", settings.stop_bits);
    printf(" Exit      : CTRL-C\r\n");
    printf("========================================\r\n\r\n");

    while (exit_requested == 0)
    {
        bytes_read = read(STDIN_FILENO, &keyboard_char, 1);

        if (bytes_read > 0)
        {
            exit_requested =
                process_keyboard_character(serial_fd,
                                           keyboard_char,
                                           command_buffer,
                                           &command_length);
        }

        bytes_read = read(serial_fd, &serial_char, 1);

        if (bytes_read > 0)
        {
            process_serial_character(serial_char);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_keyboard_config);

    close(serial_fd);

    printf("\r\nSerial terminal closed cleanly.\r\n");

    return EXIT_SUCCESS;
}

