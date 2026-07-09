/******************************************************************************
 *
 * FILE:
 *     serial_terminal.c
 *
 * DATE:
 *     2026-05-28
 *
 * PROJECT:
 *     Serial Communications Utilities
 *
 * PURPOSE:
 *     Interactive serial terminal for talking to embedded UART devices.
 *
 *     Known-good targets:
 *         1. Arduino NanoBee NANO_PWM_VENOM firmware.
 *         2. Future FPGA UART "number Hello" transmitter test.
 *
 * FLIGHT-READY DESIGN GOALS:
 *     1. Present available serial ports to the user.
 *     2. Let the user choose UART settings.
 *     3. Display incoming device text like a simple old terminal.
 *     4. Locally edit typed commands before sending them.
 *     5. Prevent Backspace/Delete bytes from corrupting device commands.
 *     6. Send only clean completed command lines to the device.
 *     7. Use select() so serial receive is fast and responsive.
 *     8. Restore the keyboard cleanly on exit.
 *
 * CHANGE LOG:
 *
 *     2026-05-28
 *         Initial serial terminal version.
 *
 *     2026-05-28
 *         Added serial port scanning.
 *
 *     2026-05-28
 *         Added interactive UART configuration menus.
 *
 *     2026-05-28
 *         Added local command-line editing.
 *
 *     2026-05-28
 *         Fixed Backspace/Delete handling so control bytes are not sent to
 *         the embedded device.
 *
 *     2026-05-28
 *         Added select() so incoming serial text displays quickly.
 *
 *     2026-05-28
 *         Expanded comments to include INPUTS, OUTPUTS, RETURNS, and
 *         SIDE EFFECTS for functions.
 *
 ******************************************************************************/

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

/******************************************************************************
 * PROGRAM LIMITS
 *
 * PURPOSE:
 *     Define fixed program limits.
 *
 * NOTES:
 *     Fixed limits are used intentionally to avoid dynamic memory allocation.
 ******************************************************************************/
#define MAX_SERIAL_PORTS        32
#define MAX_DEVICE_PATH_LENGTH  512
#define MAX_COMMAND_LENGTH      80
#define MAX_MENU_LINE_LENGTH    64

/******************************************************************************
 * ASCII CONTROL CODES
 *
 * PURPOSE:
 *     Give readable names to control characters used by the terminal.
 ******************************************************************************/
#define ASCII_BACKSPACE         0x08
#define ASCII_DELETE            0x7F
#define ASCII_CARRIAGE_RETURN   '\r'
#define ASCII_LINE_FEED         '\n'
#define ASCII_CTRL_C            0x03

/******************************************************************************
 * PARITY OPTIONS
 *
 * PURPOSE:
 *     Human-readable integer codes for serial parity selection.
 ******************************************************************************/
#define PARITY_NONE             0
#define PARITY_EVEN             1
#define PARITY_ODD              2

/******************************************************************************
 * TYPE:
 *     serial_settings_t
 *
 * PURPOSE:
 *     Holds the user-selected UART configuration.
 ******************************************************************************/
typedef struct
{
    /**************************************************************************
     * baud_rate_constant
     *
     * PURPOSE:
     *     termios baud-rate constant, such as B9600 or B115200.
     **************************************************************************/
    speed_t baud_rate_constant;

    /**************************************************************************
     * baud_rate_number
     *
     * PURPOSE:
     *     Human-readable baud-rate number printed in the status banner.
     **************************************************************************/
    int baud_rate_number;

    /**************************************************************************
     * data_bits
     *
     * PURPOSE:
     *     UART data width.
     *
     * VALID VALUES:
     *     7 or 8.
     **************************************************************************/
    int data_bits;

    /**************************************************************************
     * parity_mode
     *
     * PURPOSE:
     *     Selected UART parity mode.
     *
     * VALID VALUES:
     *     PARITY_NONE
     *     PARITY_EVEN
     *     PARITY_ODD
     **************************************************************************/
    int parity_mode;

    /**************************************************************************
     * stop_bits
     *
     * PURPOSE:
     *     UART stop-bit count.
     *
     * VALID VALUES:
     *     1 or 2.
     **************************************************************************/
    int stop_bits;

} serial_settings_t;

/******************************************************************************
 * FUNCTION:
 *     read_menu_number
 *
 * PURPOSE:
 *     Read one numeric menu selection from standard input.
 *
 * INPUTS:
 *     default_value
 *         Value returned if the user presses ENTER or enters invalid text.
 *
 * OUTPUTS:
 *     None.
 *
 * RETURNS:
 *     Parsed integer
 *         User typed a valid integer.
 *
 *     default_value
 *         User pressed ENTER, input failed, or no valid integer was found.
 *
 * SIDE EFFECTS:
 *     Reads from stdin.
 *
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
 *     Decide whether a /dev entry looks like a usable USB serial device.
 *
 * INPUTS:
 *     device_name
 *         File name from the /dev directory.
 *
 * OUTPUTS:
 *     None.
 *
 * RETURNS:
 *     1
 *         Device name appears to be a usable USB serial device.
 *
 *     0
 *         Device name is NULL or does not match accepted serial patterns.
 *
 * SIDE EFFECTS:
 *     None.
 *
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

#ifdef __APPLE__
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
 *
 * PURPOSE:
 *     Scan /dev and collect usable serial port paths.
 *
 * INPUTS:
 *     ports
 *         Two-dimensional character array where discovered device paths
 *         will be stored.
 *
 * OUTPUTS:
 *     ports
 *         Filled with zero or more full serial device paths.
 *
 * RETURNS:
 *     > 0
 *         Number of usable serial ports discovered.
 *
 *     0
 *         No usable serial ports found or /dev could not be opened.
 *
 * SIDE EFFECTS:
 *     Opens, reads, and closes the /dev directory.
 *     Prints an error message if /dev cannot be opened.
 *
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
 *     Present detected serial ports and let the user select one.
 *
 * INPUTS:
 *     ports
 *         Array containing discovered serial device paths.
 *
 *     port_count
 *         Number of valid entries in ports.
 *
 * OUTPUTS:
 *     None.
 *
 * RETURNS:
 *     >= 0
 *         Selected port array index.
 *
 *     -1
 *         Invalid user selection.
 *
 * SIDE EFFECTS:
 *     Prints menu text to stdout.
 *     Reads user input from stdin.
 *
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
 *
 * PURPOSE:
 *     Let the user select the UART baud rate.
 *
 * INPUTS:
 *     settings
 *         Pointer to UART settings structure.
 *
 * OUTPUTS:
 *     settings->baud_rate_constant
 *         Updated termios baud-rate constant.
 *
 *     settings->baud_rate_number
 *         Updated human-readable baud-rate number.
 *
 * RETURNS:
 *     None.
 *
 * SIDE EFFECTS:
 *     Prints menu text to stdout.
 *     Reads user input from stdin.
 *
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
 *     Let the user select 7-bit or 8-bit UART data width.
 *
 * INPUTS:
 *     settings
 *         Pointer to UART settings structure.
 *
 * OUTPUTS:
 *     settings->data_bits
 *         Updated to 7 or 8.
 *
 * RETURNS:
 *     None.
 *
 * SIDE EFFECTS:
 *     Prints menu text to stdout.
 *     Reads user input from stdin.
 *
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
 *
 * PURPOSE:
 *     Let the user select UART parity mode.
 *
 * INPUTS:
 *     settings
 *         Pointer to UART settings structure.
 *
 * OUTPUTS:
 *     settings->parity_mode
 *         Updated to PARITY_NONE, PARITY_EVEN, or PARITY_ODD.
 *
 * RETURNS:
 *     None.
 *
 * SIDE EFFECTS:
 *     Prints menu text to stdout.
 *     Reads user input from stdin.
 *
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
 *
 * PURPOSE:
 *     Let the user select one or two UART stop bits.
 *
 * INPUTS:
 *     settings
 *         Pointer to UART settings structure.
 *
 * OUTPUTS:
 *     settings->stop_bits
 *         Updated to 1 or 2.
 *
 * RETURNS:
 *     None.
 *
 * SIDE EFFECTS:
 *     Prints menu text to stdout.
 *     Reads user input from stdin.
 *
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
 *     configure_serial_port
 *
 * PURPOSE:
 *     Apply selected UART configuration to the opened serial port.
 *
 * INPUTS:
 *     serial_fd
 *         File descriptor for the already-opened serial device.
 *
 *     settings
 *         Pointer to selected UART settings.
 *
 * OUTPUTS:
 *     Operating system serial-port configuration is updated.
 *
 * RETURNS:
 *     0
 *         Serial port configured successfully.
 *
 *     -1
 *         Serial configuration failed.
 *
 * SIDE EFFECTS:
 *     Calls tcgetattr().
 *     Calls tcsetattr().
 *     Changes serial device behavior.
 *     Prints error messages on failure.
 *
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

    /**************************************************************************
     * FAST NON-BLOCKING SERIAL READS:
     *
     * VMIN:
     *     0 means read() may return with zero bytes.
     *
     * VTIME:
     *     0 means do not wait in 0.1-second timeout units.
     **************************************************************************/
    serial_config.c_cc[VMIN]  = 0;
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
 *     configure_keyboard_raw_mode
 *
 * PURPOSE:
 *     Put the local keyboard into raw mode after setup is complete.
 *
 * INPUTS:
 *     old_keyboard_config
 *         Pointer to structure where original keyboard settings will be saved.
 *
 * OUTPUTS:
 *     old_keyboard_config
 *         Filled with original keyboard settings.
 *
 * RETURNS:
 *     0
 *         Keyboard configured successfully.
 *
 *     -1
 *         Keyboard configuration failed.
 *
 * SIDE EFFECTS:
 *     Calls tcgetattr().
 *     Calls tcsetattr().
 *     Changes the user's terminal keyboard behavior until restored.
 *     Prints error messages on failure.
 *
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
    new_keyboard_config.c_cc[VTIME] = 0;

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
 *     Convert parity setting integer to printable text.
 *
 * INPUTS:
 *     parity_mode
 *         One of PARITY_NONE, PARITY_EVEN, or PARITY_ODD.
 *
 * OUTPUTS:
 *     None.
 *
 * RETURNS:
 *     Pointer to constant string:
 *         "None"
 *         "Even"
 *         "Odd"
 *
 * SIDE EFFECTS:
 *     None.
 *
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
 *     Locally edit a command line and send only clean completed commands
 *     to the serial device when ENTER is pressed.
 *
 * CRITICAL SAFETY RULE:
 *     Backspace/Delete edits the local command buffer only.
 *     Backspace/Delete is never sent to the embedded device.
 *
 * INPUTS:
 *     serial_fd
 *         File descriptor for the opened serial device.
 *
 *     keyboard_char
 *         One character read from the local keyboard.
 *
 *     command_buffer
 *         Local command buffer.
 *
 *     command_length
 *         Pointer to current command length.
 *
 * OUTPUTS:
 *     command_buffer
 *         Modified when the user types, erases, or submits a command.
 *
 *     command_length
 *         Updated to match the current command buffer length.
 *
 * RETURNS:
 *     0
 *         Continue program execution.
 *
 *     1
 *         User requested exit by pressing CTRL-C.
 *
 * SIDE EFFECTS:
 *     Writes local echo text to stdout.
 *     Writes completed command lines to the serial device.
 *
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
 *     Display one incoming serial character like a simple old terminal.
 *
 * INPUTS:
 *     serial_char
 *         One byte received from the embedded device.
 *
 * OUTPUTS:
 *     None.
 *
 * RETURNS:
 *     None.
 *
 * SIDE EFFECTS:
 *     Writes received text to stdout.
 *
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
 *
 * INPUTS:
 *     None.
 *
 * OUTPUTS:
 *     None.
 *
 * RETURNS:
 *     EXIT_SUCCESS
 *         Program completed normally.
 *
 *     EXIT_FAILURE
 *         Startup, configuration, or serial-port error occurred.
 *
 * SIDE EFFECTS:
 *     Scans /dev.
 *     Opens serial device.
 *     Configures serial device.
 *     Temporarily changes keyboard terminal settings.
 *     Reads keyboard input.
 *     Reads serial input.
 *     Writes serial output.
 *
 ******************************************************************************/
int main(void)
{
    /**************************************************************************
     * SERIAL PORT STORAGE
     *
     * PURPOSE:
     *     Store discovered serial port path names.
     **************************************************************************/
    char serial_ports[MAX_SERIAL_PORTS][MAX_DEVICE_PATH_LENGTH];

    /**************************************************************************
     * COMMAND BUFFER STORAGE
     *
     * PURPOSE:
     *     Store the command currently being typed by the user.
     **************************************************************************/
    char command_buffer[MAX_COMMAND_LENGTH + 1];

    /**************************************************************************
     * SERIAL PORT STATE VARIABLES
     *
     * PURPOSE:
     *     Track serial-port discovery, selection, and opened file descriptor.
     **************************************************************************/
    int port_count;
    int selected_port_index;
    int serial_fd;

    /**************************************************************************
     * PROGRAM CONTROL VARIABLES
     *
     * PURPOSE:
     *     Control the main loop and select() state.
     **************************************************************************/
    int exit_requested;
    int highest_fd;
    int select_result;

    /**************************************************************************
     * CHARACTER I/O VARIABLES
     *
     * PURPOSE:
     *     Store one keyboard byte and one serial byte at a time.
     **************************************************************************/
    unsigned char keyboard_char;
    unsigned char serial_char;

    /**************************************************************************
     * READ RESULT VARIABLE
     *
     * PURPOSE:
     *     Store return values from read().
     **************************************************************************/
    ssize_t bytes_read;

    /**************************************************************************
     * COMMAND LENGTH VARIABLE
     *
     * PURPOSE:
     *     Track how many valid characters are currently in command_buffer.
     **************************************************************************/
    size_t command_length;

    /**************************************************************************
     * SELECT FILE DESCRIPTOR SET
     *
     * PURPOSE:
     *     Tell select() which file descriptors to monitor.
     **************************************************************************/
    fd_set read_fd_set;

    /**************************************************************************
     * KEYBOARD RESTORE STORAGE
     *
     * PURPOSE:
     *     Save original terminal keyboard settings so they can be restored.
     **************************************************************************/
    struct termios old_keyboard_config;

    /**************************************************************************
     * UART SETTINGS STORAGE
     *
     * PURPOSE:
     *     Store the selected serial communication settings.
     **************************************************************************/
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

#ifdef __APPLE__
        printf("    ls /dev/cu.*\n");
#elif defined(__linux__)
        printf("    ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null\n");
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

    if (configure_keyboard_raw_mode(&old_keyboard_config) != 0)
    {
        close(serial_fd);
        return EXIT_FAILURE;
    }

    printf("\r\n");
    printf("========================================\r\n");
    printf(" SERIAL TERMINAL STARTED\r\n");
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

            perror("select failed");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &read_fd_set))
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
        }

        if (FD_ISSET(serial_fd, &read_fd_set))
        {
            bytes_read = read(serial_fd, &serial_char, 1);

            while (bytes_read > 0)
            {
                process_serial_character(serial_char);
                bytes_read = read(serial_fd, &serial_char, 1);
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_keyboard_config);

    close(serial_fd);

    printf("\r\nSerial terminal closed cleanly.\r\n");

    return EXIT_SUCCESS;
}


