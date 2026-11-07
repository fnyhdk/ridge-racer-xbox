/* The Xbox entry point.
 *
 * The runtime's own main() -- compiled from tools/m0/main.c under the name
 * m0_main -- does all the work. What is added here is what an Xbox needs and
 * a desktop does not:
 *
 *  - a screen to say what is wrong. There is no terminal: a missing PSX.EXE
 *    would otherwise send the console straight back to the dashboard, and
 *    the player would never learn why.
 *
 *  - a stack. The XBE's main thread gets 64 KB; the translated code is a
 *    chain of native calls as deep as the game's own, plus the interrupt
 *    handler entered on top of whatever is running. The game runs in a thread
 *    of its own with a megabyte.
 *
 *  - the arguments. The runtime takes "seconds" and the path of PSX.EXE from
 *    its command line; zero seconds means no time limit.
 */
#include <hal/debug.h>
#include <hal/video.h>
#include <windows.h>
#include <stdio.h>

int m0_main(int argc, char **argv);
extern const char *CD_META;

#define EXE_PATH "D:\\PSX.EXE"
#define GAME_STACK (1024 * 1024)

static DWORD WINAPI game(LPVOID unused)
{
    static char *argv[] = { "m0", "0", EXE_PATH, 0 };
    (void)unused;
    m0_main(3, argv);
    return 0;
}

static int present(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int main(void)
{
    HANDLE t;
    int ok = 1;
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    if (!present(EXE_PATH)) {
        debugPrint("Ridge Racer: %s is missing.\n"
                   "  Copy PSX.EXE from your own disc (SLPS-00001) next to default.xbe.\n\n",
                   EXE_PATH);
        ok = 0;
    }
    if (!CD_META[0] || !present(CD_META)) {
        debugPrint("Ridge Racer: the disc image %s is missing.\n"
                   "  Copy the data track of your own disc, 2048-byte sectors,\n"
                   "  next to default.xbe under that name.\n\n",
                   CD_META[0] ? CD_META : "(none built in)");
        ok = 0;
    }
    if (!ok) {
        debugPrint("Returning to the dashboard in 15 seconds.\n");
        Sleep(15000);
        return 1;
    }

    t = CreateThread(NULL, GAME_STACK, game, NULL, 0, NULL);
    if (!t) {
        debugPrint("Ridge Racer: could not start the game thread.\n");
        Sleep(15000);
        return 1;
    }
    WaitForSingleObject(t, INFINITE);
    return 0;
}
