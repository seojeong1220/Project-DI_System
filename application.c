#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <linux/i2c-dev.h>

#define CLOCK_DEV "/dev/clock_drv"


#define I2C_DEV "/dev/i2c-1"
#define OLED_I2C_ADDR 0x3C

#define OLED_W 128
#define OLED_H 64
static uint8_t fb[OLED_W * OLED_H / 8];

static int i2c_fd = -1;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int iround_div(long long sum, int cnt) {
    if (cnt <= 0) return -1;
    if (sum >= 0) return (int)((sum + cnt/2) / cnt);
    return (int)((sum - cnt/2) / cnt);
}


static int i2c_open_oled(void) {
    i2c_fd = open(I2C_DEV, O_RDWR);
    if (i2c_fd < 0) { perror("open i2c"); return -1; }
    if (ioctl(i2c_fd, I2C_SLAVE, OLED_I2C_ADDR) < 0) {
        perror("ioctl I2C_SLAVE");
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }
    return 0;
}

static void oled_cmd(uint8_t c) {
    uint8_t buf[2] = {0x00, c};
    (void)write(i2c_fd, buf, 2);
}

static void oled_data_chunk(const uint8_t *d, size_t n) {
    uint8_t buf[1 + 16];
    while (n > 0) {
        size_t m = (n > 16) ? 16 : n;
        buf[0] = 0x40;
        memcpy(&buf[1], d, m);
        (void)write(i2c_fd, buf, 1 + m);
        d += m;
        n -= m;
    }
}

static void oled_init(void) {
    oled_cmd(0xAE);
    oled_cmd(0xD5); oled_cmd(0x80);
    oled_cmd(0xA8); oled_cmd(0x3F);
    oled_cmd(0xD3); oled_cmd(0x00);
    oled_cmd(0x40);
    oled_cmd(0x8D); oled_cmd(0x14);
    oled_cmd(0x20); oled_cmd(0x00);
    oled_cmd(0xA1);
    oled_cmd(0xC8);
    oled_cmd(0xDA); oled_cmd(0x12);
    oled_cmd(0x81); oled_cmd(0xCF);
    oled_cmd(0xD9); oled_cmd(0xF1);
    oled_cmd(0xDB); oled_cmd(0x40);
    oled_cmd(0xA4);
    oled_cmd(0xA6);
    oled_cmd(0xAF);
}

static void oled_flush(void) {
    oled_cmd(0x21); oled_cmd(0); oled_cmd(127);
    oled_cmd(0x22); oled_cmd(0); oled_cmd(7);
    oled_data_chunk(fb, sizeof(fb));
}


static void fb_clear(void) { memset(fb, 0, sizeof(fb)); }

static void fb_set_px(int x, int y, int on) {
    if (x<0||x>=OLED_W||y<0||y>=OLED_H) return;
    int idx = (y/8)*OLED_W + x;
    uint8_t mask = (1u << (y%8));
    if (on) fb[idx] |= mask;
    else    fb[idx] &= ~mask;
}

static const uint8_t font5x7_digits[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E}
};
static const uint8_t font_colon[5] = {0x00,0x36,0x36,0x00,0x00};

static const uint8_t font_E[5]={0x7F,0x49,0x49,0x49,0x41};
static const uint8_t font_D[5]={0x7F,0x41,0x41,0x22,0x1C};
static const uint8_t font_I[5]={0x00,0x41,0x7F,0x41,0x00};
static const uint8_t font_T[5]={0x01,0x01,0x7F,0x01,0x01};
static const uint8_t font_R[5]={0x7F,0x09,0x19,0x29,0x46};
static const uint8_t font_U[5]={0x3F,0x40,0x40,0x40,0x3F};
static const uint8_t font_N[5]={0x7F,0x06,0x18,0x60,0x7F};
static const uint8_t font_W[5]={0x7F,0x20,0x18,0x20,0x7F};
static const uint8_t font_H[5]={0x7F,0x08,0x08,0x08,0x7F};
static const uint8_t font_M[5]={0x7F,0x02,0x04,0x02,0x7F};
static const uint8_t font_S[5]={0x46,0x49,0x49,0x49,0x31};
static const uint8_t font_G[5]={0x3E,0x41,0x41,0x51,0x32};
static const uint8_t font_P[5]={0x7F,0x09,0x09,0x09,0x06};
static const uint8_t font_A[5]={0x7E,0x09,0x09,0x09,0x7E};
static const uint8_t font_B[5]={0x7F,0x49,0x49,0x49,0x36};




static const uint8_t font_a[5]={0x20,0x54,0x54,0x54,0x78};
static const uint8_t font_c[5]={0x38,0x44,0x44,0x44,0x20};
static const uint8_t font_d[5]={0x38,0x44,0x44,0x48,0x7F};
static const uint8_t font_e[5]={0x38,0x54,0x54,0x54,0x18};
static const uint8_t font_g[5]={0x18,0xA4,0xA4,0xA4,0x7C};
static const uint8_t font_h[5]={0x7F,0x08,0x04,0x04,0x78};
static const uint8_t font_i[5]={0x00,0x44,0x7D,0x40,0x00};
static const uint8_t font_l[5]={0x00,0x41,0x7F,0x40,0x00};
static const uint8_t font_m[5]={0x7C,0x04,0x18,0x04,0x78};
static const uint8_t font_n[5]={0x7C,0x08,0x04,0x04,0x78};
static const uint8_t font_o[5]={0x38,0x44,0x44,0x44,0x38};
static const uint8_t font_p[5]={0x7C,0x14,0x14,0x14,0x08};
static const uint8_t font_r[5]={0x7C,0x08,0x04,0x04,0x08};
static const uint8_t font_s[5]={0x48,0x54,0x54,0x54,0x20};
static const uint8_t font_t[5]={0x04,0x3F,0x44,0x40,0x20};
static const uint8_t font_u[5]={0x3C,0x40,0x40,0x20,0x7C};
static const uint8_t font_w[5]={0x3C,0x40,0x30,0x40,0x3C};
static const uint8_t font_y[5]={0x0C,0x50,0x50,0x50,0x3C};
static const uint8_t font_space[5] = {0,0,0,0,0};

static const uint8_t font_percent[5] = {0x23, 0x13, 0x08, 0x64, 0x62};
static const uint8_t font_deg[5] = {0x06, 0x09, 0x09, 0x06, 0x00};
static const uint8_t font_dot[5] = {0x00,0x60,0x60,0x00,0x00};


static const uint8_t icon_good[8]   = { 0x3C,0x42,0xA5,0x81,0xA5,0x99,0x42,0x3C };
static const uint8_t icon_Mild[8] = { 0x3C,0x42,0xA5,0x81,0xBD,0x81,0x42,0x3C };
static const uint8_t icon_bad[8]    = { 0x3C,0x42,0xA5,0x81,0x99,0xA5,0x42,0x3C };
static const uint8_t icon_hot[8]    = { 0x3C,0x42,0xA5,0x81,0x9D,0xA1,0x42,0x3C };

static const uint8_t* glyph_for_char(char c) {
    if (c>='0' && c<='9') return font5x7_digits[c-'0'];
    if (c==':') return font_colon;

    if (c=='E') return font_E; if (c=='D') return font_D; if (c=='I') return font_I;
    if (c=='T') return font_T; if (c=='R') return font_R; if (c=='U') return font_U;
    if (c=='N') return font_N; if (c=='W') return font_W; if (c=='H') return font_H;
    if (c=='M') return font_M; if (c=='S') return font_S; if (c=='G') return font_G;
    if (c=='P') return font_P; if (c=='A') return font_A; if (c=='B') return font_B;


    if (c=='a') return font_a; if (c=='c') return font_c; if (c=='d') return font_d;
    if (c=='e') return font_e; if (c=='g') return font_g; if (c=='h') return font_h;
    if (c=='i') return font_i; if (c=='l') return font_l; if (c=='m') return font_m;
    if (c=='n') return font_n; if (c=='o') return font_o; if (c=='p') return font_p;
    if (c=='r') return font_r; if (c=='s') return font_s; if (c=='t') return font_t;
    if (c=='u') return font_u; if (c=='w') return font_w; if (c=='y') return font_y;
    if (c=='%') return font_percent; if (c==0xB0) return font_deg; if (c=='.') return font_dot;
    if (c==' ') return font_space;
    return NULL;
}

static void fb_draw_char5x7(int x, int y, char c, int scale) {
    const uint8_t *g = glyph_for_char(c);
    if (!g) return;
    for (int i=0;i<5;i++) {
        for (int j=0;j<7;j++) {
            if ((g[i] >> j) & 1) {
                for (int sx=0;sx<scale;sx++)
                    for (int sy=0;sy<scale;sy++)
                        fb_set_px(x+i*scale+sx, y+j*scale+sy, 1);
            }
        }
    }
}

static void fb_draw_text(int x, int y, const char *s, int scale, int spacing) {
    while (*s) {
        fb_draw_char5x7(x, y, *s++, scale);
        x += 5*scale + spacing;
    }
}

static int di_to_led(int di)
{
    if (di < 65) return 0;
    if (di > 80) return 8;

    return 1 + (di - 65) * 7 / 15;
}

static void set_led_level(int level)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "LED %d", level);

    int fd = open(CLOCK_DEV, O_WRONLY);
    if (fd >= 0) {
        write(fd, buf, strlen(buf));
        close(fd);
    }
}

static void fb_draw_icon8(int x, int y, const uint8_t icon[8], int scale) {
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            if (icon[r] & (1 << c)) {
                for (int sx = 0; sx < scale; sx++) {
                    for (int sy = 0; sy < scale; sy++) {
                        fb_set_px(x + c*scale + sx, y + r*scale + sy, 1);
                    }
                }
            }
        }
    }
}

static void fb_draw_circle(int cx,int cy,int r,int filled){
    for(int y=-r;y<=r;y++){
        for(int x=-r;x<=r;x++){
            int d=x*x+y*y;
            int rr=r*r;
            if(filled){ if(d<=rr) fb_set_px(cx+x,cy+y,1); }
            else{ if(d>=rr-r && d<=rr+r) fb_set_px(cx+x,cy+y,1); }
        }
    }
}
static void draw_page_dots(int page){
    int cy=60,r=3;
    int cx1=OLED_W/2-15;
    int cx2=OLED_W/2;
    int cx3=OLED_W/2+15;
    fb_draw_circle(cx1,cy,r,0);
    fb_draw_circle(cx2,cy,r,0);
    fb_draw_circle(cx3,cy,r,0);
    if(page==0) fb_draw_circle(cx1,cy,r-1,1);
    else if(page==1) fb_draw_circle(cx2,cy,r-1,1);
    else fb_draw_circle(cx3,cy,r-1,1);
}

static int read_clock_line(char *out, size_t outsz) {
    int fd = open(CLOCK_DEV, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, out, outsz-1);
    close(fd);
    if (n <= 0) return -1;
    out[n] = 0;
    return 0;
}

int main(void) {
    if (i2c_open_oled() != 0) return 1;
    oled_init();

    int blink = 0;
    int prev_page = 0;

    long long last_dht_ms = 0;
    int cur_temp = -1;
    int cur_hum  = -1;

    while (1) {
        char line[200];
        char mode[8]="RUN";
        char field[8]="SEC";
        int hh=0,mm=0,ss=0,page=0;
        int temp=-1,hum=-1;

        if (read_clock_line(line,sizeof(line))==0) {
            sscanf(line,"%d:%d:%d MODE=%7s FIELD=%7s PAGE=%d TEMP=%d HUM=%d",
                   &hh,&mm,&ss,mode,field,&page,&temp,&hum);
        }

        long long now = now_ms();

        
        if ((page == 1 || page == 2) && (now - last_dht_ms >= 2000)) {
            if (temp >= 0 && hum >= 0) {
                cur_temp = temp;
                cur_hum  = hum;
            }
            last_dht_ms = now;
        }

        fb_clear();
        draw_page_dots(page);

        
        if (page==0) {
            if (strcmp(mode,"EDIT")==0)
                fb_draw_text(0,0,"EDIT",1,1);
            else
                fb_draw_text(0,0,"RUN",1,1);

            if (strcmp(mode,"EDIT")==0) {
                char hs[3], ms[3], ss_s[3];
                sprintf(hs,"%02d",hh);
                sprintf(ms,"%02d",mm);
                sprintf(ss_s,"%02d",ss);

                if (!(strcmp(field,"HOUR")==0 && blink)) fb_draw_text(10,18,hs,2,2);
                fb_draw_text(34,18,":",2,2);
                if (!(strcmp(field,"MIN")==0 && blink)) fb_draw_text(46,18,ms,2,2);
                fb_draw_text(70,18,":",2,2);
                if (!(strcmp(field,"SEC")==0 && blink)) fb_draw_text(82,18,ss_s,2,2);
            } else {
                char ts[16];
                sprintf(ts,"%02d:%02d:%02d",hh,mm,ss);
                fb_draw_text(10,18,ts,2,2);
            }
        }

        
        else if (page==1) {
            const int num_x=92;

            fb_draw_text(0,0,"Today Weather",1,1);
            fb_draw_text(0,20,"Temp:",1,1);
            fb_draw_text(0,38,"Humidity:",1,1);

            char tstr[8], hstr[8];
            
            if (cur_temp >= 0) sprintf(tstr,"%02d",cur_temp);
            else strcpy(tstr,"--");

            if (cur_hum >= 0) sprintf(hstr,"%02d",cur_hum);
            else strcpy(hstr,"--");

            fb_draw_text(num_x,16,tstr,2,2);
            fb_draw_text(num_x+22,16,"\xB0",1,1);   
            fb_draw_text(num_x+30,16,"C",1,1);      

            fb_draw_text(num_x,34,hstr,2,2);
            fb_draw_text(num_x+24,34,"%",1,1);     
        }

        
        else {
            int di=-1;
            if (cur_temp >= 0 && cur_hum >= 0)
                di = (int)(0.81*cur_temp + 0.01*cur_hum*(0.99*cur_temp-14.3) + 46.3);

            fb_draw_text(0,0,"DI PAGE",1,1);


            char di_str[8];
            if (di>=0) sprintf(di_str,"%02d",di);
            else strcpy(di_str,"--");

            fb_draw_text(0,20,"DI:",2,2);        
            fb_draw_text(40,20,di_str,2,2);    

        if (di >= 0) {
            int led = di_to_led(di);
            set_led_level(led);
        }

        if (di < 0) {
            fb_draw_text(40,14,"--",2,2);
            fb_draw_text(0,36,"No Data",2,2);
        }
        else if (di < 68) {
            fb_draw_icon8(90,20,icon_good,2);
            fb_draw_text(0,36,"Good",2,2);
        }
        else if (di < 75) {
            fb_draw_icon8(90,20,icon_Mild,2);
            fb_draw_text(0,36,"Mild",2,2);
        }
        else if (di < 80) {
            fb_draw_icon8(90,20,icon_bad,2);
            fb_draw_text(0,36,"Bad",2,2);
        }
        else {
            fb_draw_icon8(90,20,icon_hot,2);
            fb_draw_text(0,36,"Hot",2,2);
        }

    }

        oled_flush();
        blink = !blink;
        prev_page = page;
        usleep(200000);
    }
    return 0;
}
