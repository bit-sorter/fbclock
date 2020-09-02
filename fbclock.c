/*
fbclock - A digital clock at the bottom of your framebuffer console.
Copyright (C) 2020  Craig McPartland

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "font8x8_basic.h"

#define FONT_WIDTH 8
#define FONT_HEIGHT 8
#define MAX_TEXT_LEN 128

typedef struct {
  char *capacity; /* e.g. "/sys/class/power_supply/BAT0/capacity" */
  char *fbdev;
  char *title;
  int file;
  struct fb_fix_screeninfo finfo;
  struct fb_var_screeninfo vinfo;
  void *video;
} FBClock;

int g_running = 1;

int get_framebuffer_info(FBClock *clock)
{
  int status = ioctl(clock->file, FBIOGET_FSCREENINFO, &clock->finfo);

  if (status == -1) {
    fprintf(stderr, "%s: ioctl (FBIOGET_FSCREENINFO) in %s failed. \n",
            clock->title, __func__);

    return status;
  }

  status = ioctl(clock->file, FBIOGET_VSCREENINFO, &clock->vinfo);

  if (status == -1) {
    fprintf(stderr, "%s: ioctl (FBIOGET_VSCREENINFO) in %s failed. \n",
            clock->title, __func__);
  }

  return status;
}

int map_framebuffer(FBClock *clock)
{
  int status;

  clock->file = open(clock->fbdev, O_RDWR | O_NONBLOCK);

  if (clock->file == -1) {
    fprintf(stderr, "%s: open(\"%s\", O_RDWR | O_NONBLOCK) in %s failed. \n",
            clock->title, clock->fbdev, __func__);
    return 1;
  }

  status = get_framebuffer_info(clock);

  if (status != -1) {
    clock->video = mmap(NULL, clock->finfo.smem_len,
                   PROT_READ | PROT_WRITE, MAP_SHARED, clock->file, 0);

    if (clock->video != MAP_FAILED) {
      return 0;
    } else {
      close(clock->file);
      fprintf(stderr, "%s: mmap in %s failed. \n", clock->title, __func__);
    }
  }

  return 1;
}

void draw_char(FBClock *clock, int x, int y, int ch)
{
  int bytes_per_pixel, char_row;
  unsigned char *video_offset;

  bytes_per_pixel = clock->vinfo.bits_per_pixel / 8;
  video_offset = (unsigned char *)clock->video +
                 y * clock->finfo.line_length + x * bytes_per_pixel;

  for (char_row = 0; char_row < FONT_HEIGHT; char_row++) {
    int char_bit = 1;

    while (char_bit < 256) {
      if (font8x8_basic[ch][char_row] & char_bit) {
        memset(video_offset, 0xff, bytes_per_pixel);
      } else {
        memset(video_offset, 0x0, bytes_per_pixel);
      }

      video_offset += bytes_per_pixel;
      char_bit <<= 1;
    }

    video_offset += clock->finfo.line_length - FONT_WIDTH * bytes_per_pixel;
  }
}

void clear_time(FBClock *clock, int y)
{
  unsigned char *video_offset = (unsigned char *)clock->video +
                                y * clock->finfo.line_length;
  memset(video_offset, 0, FONT_HEIGHT * clock->finfo.line_length);
}

void get_asctime(char *text)
{
  time_t now = time(NULL);

  if (now != (time_t)-1) {
    char *time_string = NULL;
    const struct tm *local = localtime(&now);

    if (local != NULL) {
      time_string = asctime(local);
    }

    if (time_string != NULL) {
      strcpy(text, time_string);
      return;
    }
  }

  strcpy(text, "Error getting time!");
}

void add_power_level_to_text(FBClock *clock, char *text)
{
  FILE *file = fopen(clock->capacity, "r");

  if (file != NULL) {
    char capacity[4];
    text += strlen(text);
    fgets(capacity, 4, file);
    sprintf(text, "- %d%%", atoi(capacity)); /* atoi to get rid of newline. */
    fclose(file);
  }
}

void run_clock(FBClock *clock)
{
  char text[MAX_TEXT_LEN];
  int dx, i, x, y;

  dx = 1;
  x = 5;
  y = clock->vinfo.yres - FONT_HEIGHT - 2;

  while (g_running) {
    get_asctime(text);

    if (clock->capacity != NULL) {
      add_power_level_to_text(clock, text);
    }

    i = strlen(text) - 1;
    clear_time(clock, y);

    while (i >= 0) {
      int p = x + i * FONT_WIDTH;
      draw_char(clock, p, y, text[i]);
      i--;
    }

    x += dx;

    if (x > 20 || x < 6) {
      dx = -dx;
    }

    sleep(1);
  }

  munmap(clock->video, clock->finfo.smem_len);
  close(clock->file);
}

void handle_signal(int sig)
{
  g_running = 0;
}

int get_arguments(FBClock *clock, int argc, char *argv[])
{
  int opt;

  clock->capacity = NULL;
  clock->fbdev = "/dev/fb0";

  while ((opt = getopt(argc, argv, "b:f:")) != -1) {
    switch (opt) {
      case 'b':
        clock->capacity = strdup(optarg);
        break;
      case 'f':
        clock->fbdev = strdup(optarg);
        break;
      default:
        fprintf(stderr, "Usage: %s [-b PATH] [ -f DEVICE ]\n", clock->title);
        fprintf(stderr, "PATH is path to battery capacity file.\n");
        fprintf(stderr, "DEVICE is framebuffer device (default /dev/fb0).\n");
        return 1;
    }
  }

  return 0;
}

void get_title(FBClock *clock, char *path)
{
  clock->title = strrchr(path, '/');

  if (clock->title == NULL) {
    clock->title = path;
  } else {
    clock->title++;
  }
}

int main(int argc, char *argv[])
{
  FBClock clock;
  int status;
  pid_t pid, sid;

  get_title(&clock, argv[0]);
  status = get_arguments(&clock, argc, argv);

  if (status != 0) {
    return 1;
  }

  status = map_framebuffer(&clock);

  if (status != 0) {
    return 1;
  }

  pid = fork();

  if (pid < 0) {
    return 1;
  }

  if (pid > 0) {
    return 0;
  }

  sid = setsid();

  if (sid < 0) {
    return 2;
  }

  chdir("/");
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  signal(SIGINT, handle_signal);
  run_clock(&clock);

  return 0;
}

