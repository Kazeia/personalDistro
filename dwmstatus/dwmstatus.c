#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h> //para setenv()
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/Xlib.h>

#define BATTERY "/sys/class/power_supply/BAT0/capacity"
#define VOLCMD "echo $(amixer get Master | tail -n1 | sed -r 's/.*\\[(.*)%\\].*/\\1/')%" //chekar si debemos eliminar el ultimo %
#define RXWCMD "cat /sys/class/net/wlan0/statistics/rx_bytes"
#define TXWCMD "cat /sys/class/net/wlan0/statistics/tx_bytes"

/*
chekar siguientes archivos si existen con cat, puedes chekar q variables saka de ese archivo tmb para saber si estan:
/proc/stat
/proc/meminfo
*/
static Display *dpy;
static const char *suffixes[] = {"KiB", "MiB", "GiB", "TiB", "PiB", ""};
static unsigned long long lastTotalUser[4], lastTotalUserLow[4], lastTotalSys[4], lastTotalIdle[4];

char *tzpst = "America/Lima"; //timezone especificar, chekar en tabla
char trash[5];

char *smprintf(char *fmt, ...) { //supongo q imprime en el dwmenu
    va_list fmtargs;
    char *ret;
    int len;

    va_start(fmtargs, fmt);
    len = vsnprintf(NULL, 0, fmt, fmtargs);
    va_end(fmtargs);

    ret = malloc(++len);
    if (ret == NULL) {
        perror("malloc");
        exit(1);
    }

    va_start(fmtargs, fmt);
    vsnprintf(ret, len, fmt, fmtargs);
    va_end(fmtargs);

    return ret;
}

void settz(char *tzname) {
    setenv("TZ", tzname, 1);
}

char *mktimes(char *fmt, char *tzname) {
    char buf[129];
    time_t tim;
    struct tm *timtm;

    bzero(buf, sizeof (buf));
    settz(tzname);
    tim = time(NULL);
    timtm = localtime(&tim);
    if (timtm == NULL) {
        perror("localtime");
        exit(1);
    }

    if (!strftime(buf, sizeof (buf) - 1, fmt, timtm)) {
        fprintf(stderr, "strftime == 0\n");
        exit(1);
    }

    return smprintf("%s", buf);
}

void setstatus(char *str) {
    XStoreName(dpy, DefaultRootWindow(dpy), str);
    XSync(dpy, False);
}

char *getbattery(char *path) {
    char batteryLevel[5];
    char *batStatus = 0;
    FILE *fd;

    fd = fopen(path, "r");
    if (fd != NULL) {

        fgets(batteryLevel, 3, fd);
        int batPercent = atoi(batteryLevel);
        fclose(fd);

        if (batPercent > 80) batStatus = smprintf("%s %d%%%c", "", batPercent, '\x05');
        else if (batPercent > 50) batStatus = smprintf("%s %d%%%c", "", batPercent, '\x06');
        else batStatus = smprintf("%s %d%%%c", "", batPercent, '\x07');
    } else {
        batStatus = smprintf("%s%c", "", '\x05');
    }

    return batStatus;
}

char *runcmd(char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (fp == NULL) return NULL;
    char ln[30];
    fgets(ln, sizeof (ln) - 1, fp);
    pclose(fp);
    ln[strlen(ln) - 1] = '\0';
    return smprintf("%s", ln);
}

void initcore() {
    FILE* file = fopen("/proc/stat", "r");
    char ln[100];

    for (int i = 0; i < 5; i++) {
        fgets(ln, 99, file);
        if (i < 1) continue;
        sscanf(ln, "%s %llu %llu %llu %llu", trash, &lastTotalUser[i - 1],&lastTotalUserLow[i - 1], &lastTotalSys[i - 1],&lastTotalIdle[i - 1]);
    }
    fclose(file);
}

void getcore(char cores[4][6]) {
    double percent;
    FILE* file;
    unsigned long long totalUser[4], totalUserLow[4], totalSys[4], totalIdle[4], total[2];

    char ln[100];

    file = fopen("/proc/stat", "r");
    for (int i = 0; i < 5; i++) {
        fgets(ln, 99, file);
        if (i < 1) continue;
        sscanf(ln, "%s %llu %llu %llu %llu", trash, &totalUser[i - 1],&totalUserLow[i - 1], &totalSys[i - 1], &totalIdle[i - 1]);
    }
    fclose(file);

    for (int i = 0; i < 4; i++) {
        if (totalUser[i] < lastTotalUser[i] || totalUserLow[i] < lastTotalUserLow[i]|| totalSys[i] < lastTotalSys[i] || totalIdle[i] < lastTotalIdle[i]) {
            //Overflow detection. Just skip this value.
            percent = -1.0;
        } else {
            total[i] = (totalUser[i] - lastTotalUser[i])+ (totalUserLow[i] - lastTotalUserLow[i])+ (totalSys[i] - lastTotalSys[i]);
            percent = total[i];
            total[i] += (totalIdle[i] - lastTotalIdle[i]);
            percent /= total[i];
            percent *= 100;
        }

        if (percent > 70)strcpy(cores[i], smprintf("%d%%%c", (int) percent, '\x07'));
        else if (percent > 50)strcpy(cores[i], smprintf("%d%%%c", (int) percent, '\x06'));
        else strcpy(cores[i], smprintf("%d%%%c", (int) percent, '\x05'));
    }

    for (int i = 0; i < 4; i++) {
        lastTotalUser[i] = totalUser[i];
        lastTotalUserLow[i] = totalUserLow[i];
        lastTotalSys[i] = totalSys[i];
        lastTotalIdle[i] = totalIdle[i];
    }

}

char *getmem() {
    FILE *fd;
    long total, free, avail, buf, cache, use;
    int used;
    const char **suffix = suffixes;

    fd = fopen("/proc/meminfo", "r");
    fscanf(fd,"MemTotal: %ld kB\nMemFree: %ld kB\nMemAvailable: %ld kB\nBuffers: %ld kB\nCached: %ld kB\n",&total, &free, &avail, &buf, &cache);
    fclose(fd);
    use = total - avail - buf;
    used = 100 * (use) / total;

    // Use suffixes like conky
    while (llabs(use / 1024) >= 1000LL && **(suffix + 2)) {
        use /= 1024;
        suffix++;
    }

    suffix++;
    float fuse = use / 1024.0;

    if (used > 70) return smprintf("%d%% (%.2f %s)\x07", used, fuse, *suffix);
    else if (used > 50) return smprintf("%d%% (%.2f %s)\x06", used, fuse, *suffix);
    else return smprintf("%d%% (%.2f %s)\x05", used, fuse, *suffix);
}

int main(void) {
    char *status;
    char *bat;
    char *date;
    char *tme;
    char *vol;
    char cores[4][6];
    char *mem;
    char *rxw_old, *rxw_now, *txw_old, *txw_now; //solo wireless, si keremos mostrar el de cable ethernet, seria eth0, chekar el script total, no este
    initcore();
    int rxw_rate, txw_rate; //kilo bytes
    if (!(dpy = XOpenDisplay(NULL))) {
        fprintf(stderr, "dwmstatus: cannot open display.\n");
        return 1;
    }
    rxw_old = runcmd(RXWCMD);
    txw_old = runcmd(TXWCMD);
    for (;; sleep(1)) {
        bat = getbattery(BATTERY);
        date = mktimes("%d-%m-%y", tzpst);
        tme = mktimes("%k.%M", tzpst);
        vol = runcmd(VOLCMD);
        mem = getmem();
        rxw_now = runcmd(RXWCMD);
        txw_now = runcmd(TXWCMD);
        rxw_rate = (atoi(rxw_now) - atoi(rxw_old)) / 1024;
        txw_rate = (atoi(txw_now) - atoi(txw_old)) / 1024;
        getcore(cores);
        status =smprintf("[\x01 %s ][ \x01  %dK\x02 /\x01 %dK\x02][\x01  %s\x04 ][\x01  %s /\x01 %s /\x01 %s /\x01 %s ][\x01  %s\x03 ][\x01  %s | %s ]\x01",bat, rxw_rate, txw_rate, vol, cores[0], cores[1], cores[2], cores[3], mem, date, tme);
        strcpy(rxw_old, rxw_now);
        strcpy(txw_old, txw_now);
        printf("%h:%m:%s\n", status); //chekear q sale en la consola!!!
        setstatus(status);
        free(rxw_now);
        free(txw_now);
        free(bat);
        free(vol);
        free(date);
        free(status);
        free(mem);
    }
    XCloseDisplay(dpy);
    return 0;
}
