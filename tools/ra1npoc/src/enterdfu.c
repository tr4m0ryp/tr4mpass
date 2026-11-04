/*
 * ra1npoc - enterdfu.c
 *
 * Copyright (c) 2022 dora2ios
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <io/iousb.h>
#include <common/common.h>
#include <common/log.h>

#include <getopt.h>

io_client_t client = NULL;
bool debug_enabled = false;

static void usage(char** argv)
{
    printf("Usage: %s [option]\n", argv[0]);

    printf("  -h, --help\t\t\t\x1b[36mshow usage\x1b[39m\n");
    printf("  -c, --cleandfu\t\t\x1b[36menter cleandfu [BETA]\x1b[39m\n");
    printf("  -d, --debug\t\t\t\x1b[36menable debug log\x1b[39m\n");
    printf("\n");
}

int main(int argc, char** argv)
{
    bool useRecovery = false;
    
    int opt = 0;
    static struct option longopts[] = {
        { "help",           no_argument,       NULL, 'h' },
        { "cleandfu",       no_argument,       NULL, 'c' },
        { "debug",          no_argument,       NULL, 'd' },
        { NULL, 0, NULL, 0 }
    };
    
    if(argc < 2) {
        usage(argv);
        return 0;
    }
        
    
    while ((opt = getopt_long(argc, argv, "hdc", longopts, NULL)) > 0) {
        switch (opt) {
            case 'h':
                usage(argv);
                return 0;
                
            case 'd':
                debug_enabled = true;
                DEBUGLOG("enabled: debug log");
                break;
                
            case 'c':
                useRecovery = true;
                break;
                
            default:
                usage(argv);
                return -1;
        }
    }
    
    io_reconnect(&client, 1, DEVICE_DFU, USB_RESET|USB_REENUMERATE, false, 0);
    if(client) {
        LOG("already DFU mode");
        return 0;
    }
    
    if(useRecovery) {
        if(enter_dfu_via_recovery(client)) {
            ERROR("Failed to connect to DFU mode");
            return -1;
        }
    } else {
        usage(argv);
        return -1;
    }
    
    LOG("reconnecting");
    io_reconnect(&client, 10, DEVICE_DFU, USB_RESET|USB_REENUMERATE, false, 10000);
    if(!client) {
        ERROR("Failed to connect to DFU mode");
        return -1;
    }
    LOG("SUCCESS: CONNECTED DFU mode");
    io_close(client);
    
    return 0;
}

