#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <stdbool.h>
#include <fcntl.h>
#include <libgen.h>
#include <time.h>
#include <syslog.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>

bool recursive = false, forced_sync = false;
long int threshold;

void signal_handler(int sig)
{
     if (sig == SIGUSR1)
     {
          openlog("DAEMON_SIGNAL", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Otrzymano sygnal SIGUSR1");
          closelog();
          forced_sync = true;
          return;
     }
}

void copySmallFile(char *path_to_src, char *path_to_dest)
{
     char buffer[1024];
     int src_file;
     int dest_file;
     int bytes;

     if ((src_file = open(path_to_src, O_RDONLY)) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     if ((dest_file = open(path_to_dest, O_WRONLY | O_CREAT)) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }

     while ((bytes = read(src_file, buffer, sizeof(buffer))) > 0)
     {
          write(dest_file, buffer, bytes);
     }

     if (close(src_file) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     if (close(dest_file) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }
}

void copyBigFile(char *path_to_src, char *path_to_dest)
{
     int src_file;
     int dest_file;
     char *src_mmap, *dest_mmap;
     size_t file_size;

     src_file = open(path_to_src, O_RDONLY);

     if (src_file == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     file_size = lseek(src_file, 0, SEEK_END);

     if (file_size == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas pobierania wielkosci pliku zrodlowgo: %s", path_to_src);
          closelog();
          return;
     }

     src_mmap = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, src_file, 0);

     if (src_mmap == MAP_FAILED)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas mapowania pliku zrodlowgo: %s", path_to_src);
          closelog();
          return;
     }

     dest_file = open(path_to_dest, O_RDWR | O_CREAT);

     if (dest_file == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }

     ftruncate(dest_file, file_size);

     dest_mmap = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, dest_file, 0);

     if (dest_mmap == MAP_FAILED)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas mapowania pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }

     if (memcpy(dest_mmap, src_mmap, file_size) == NULL)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas kopiowania pamieci");
          closelog();
          return;
     }

     if (munmap(src_mmap, file_size) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zwalniania pamieci pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     if (munmap(dest_mmap, file_size) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zwalniania pamieci pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }

     if (close(src_file) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     if (close(dest_file) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }
}

void clearDirectory(char *path_to_dest)
{
     DIR *dest_dir;
     struct dirent *dest_path_info;

     if ((dest_dir = opendir(path_to_dest)) == NULL)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania katalogu: %s", path_to_dest);
          closelog();
          return;
     }

     while ((dest_path_info = readdir(dest_dir)) != NULL)
     {
          if (strcmp(dest_path_info->d_name, ".") == 0 || strcmp(dest_path_info->d_name, "..") == 0)
          {
               continue;
          }

          char sub_dir[257];
          snprintf(sub_dir, sizeof(sub_dir), "%s/%s", path_to_dest, dest_path_info->d_name);

          if (dest_path_info->d_type == DT_DIR)
          {
               clearDirectory(sub_dir);
               rmdir(sub_dir);
               openlog("DAEMON_DELETE", LOG_PID | LOG_CONS, LOG_USER);
               syslog(LOG_INFO, "Usunieto katalog: %s", sub_dir);
               closelog();
          }

          else if (dest_path_info->d_type == DT_REG)
          {
               remove(sub_dir);
               openlog("DAEMON_DELETE", LOG_PID | LOG_CONS, LOG_USER);
               syslog(LOG_INFO, "Usunieto plik: %s", sub_dir);
               closelog();
          }
     }
}

void syncDirectory(char *src_path, char *dest_path)
{
     DIR *src_dir, *dest_dir;
     struct dirent *src_path_info, *dest_path_info;

     if ((src_dir = opendir(src_path)) == NULL)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania katalogu zrodlowego: %s", src_path);
          closelog();
          return;
     }

     if ((dest_dir = opendir(dest_path)) == NULL)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania katalogu docelowego: %s", dest_path);
          closelog();
          return;
     }

     openlog("DAEMON_CHECK_DIR", LOG_PID | LOG_CONS, LOG_USER);
     syslog(LOG_INFO, "Sprawdzono poprawnosc katalogow: %s, %s", src_path, dest_path);
     closelog();

     while ((src_path_info = readdir(src_dir)) != NULL)
     {
          char path_to_src[257], path_to_dest[257];
          snprintf(path_to_src, sizeof(path_to_src), "%s/%s", src_path, src_path_info->d_name);
          snprintf(path_to_dest, sizeof(path_to_dest), "%s/%s", dest_path, src_path_info->d_name);

          struct stat src_path_stat, dest_path_stat;
          stat(path_to_src, &src_path_stat);
          stat(path_to_dest, &dest_path_stat);

          if (src_path_info->d_type == DT_DIR && recursive == true)
          {
               if (access(path_to_dest, F_OK) != 0)
               {
                    mkdir(path_to_dest, src_path_stat.st_mode);
                    openlog("DAEMON_MKDIR", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Utworzono nowy katalog w folderze docelowym: %s", path_to_dest);
                    closelog();
               }

               if (strcmp(src_path_info->d_name, ".") != 0 && strcmp(src_path_info->d_name, "..") != 0)
               {
                    syncDirectory(path_to_src, path_to_dest);
               }
          }

          else if (src_path_info->d_type == DT_REG)
          {
               if (access(path_to_src, F_OK) == 0 && access(path_to_dest, F_OK) != 0)
               {
                    if (src_path_stat.st_size < threshold)
                    {
                         copySmallFile(path_to_src, path_to_dest);
                    }

                    else if (src_path_stat.st_size > threshold)
                    {
                         copyBigFile(path_to_src, path_to_dest);
                    }

                    openlog("DAEMON_COPY", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Skopiowano plik: %s", path_to_src);
                    closelog();
               }

               else if (access(path_to_src, F_OK) == 0 && access(path_to_dest, F_OK) == 0)
               {
                    time_t src_file_mtime = src_path_stat.st_mtime, dest_file_mtime = dest_path_stat.st_mtime;

                    if (difftime(src_file_mtime, dest_file_mtime) > 0 || src_path_stat.st_size != dest_path_stat.st_size)
                    {
                         remove(path_to_dest);

                         if (src_path_stat.st_size < threshold)
                         {
                              copySmallFile(path_to_src, path_to_dest);
                         }

                         else if (src_path_stat.st_size > threshold)
                         {
                              copyBigFile(path_to_src, path_to_dest);
                         }

                         openlog("DAEMON_COPY", LOG_PID | LOG_CONS, LOG_USER);
                         syslog(LOG_INFO, "Zaktualizowano plik: %s", path_to_dest);
                         closelog();
                    }
               }
          }
     }

     while ((dest_path_info = readdir(dest_dir)) != NULL)
     {
          char path_to_src[257], path_to_dest[257];
          snprintf(path_to_src, sizeof(path_to_src), "%s/%s", src_path, dest_path_info->d_name);
          snprintf(path_to_dest, sizeof(path_to_dest), "%s/%s", dest_path, dest_path_info->d_name);

          if (access(path_to_dest, F_OK) == 0 && access(path_to_src, F_OK) != 0)
          {
               if (dest_path_info->d_type == DT_REG)
               {
                    remove(path_to_dest);
                    openlog("DAEMON_DELETE", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Usunieto plik: %s", path_to_dest);
                    closelog();
               }

               else if (dest_path_info->d_type == DT_DIR && recursive == true)
               {
                    clearDirectory(path_to_dest);
                    rmdir(path_to_dest);
                    openlog("DAEMON_DELETE", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Usunieto katalog: %s", path_to_dest);
                    closelog();
               }
          }
     }

     if (closedir(src_dir) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania katalogu zrodlowego: %s", src_path);
          closelog();
          return;
     }

     if (closedir(dest_dir) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania katalogu docelowego: %s", dest_path);
          closelog();
          return;
     }
}

int main(int argc, char *argv[])
{
     char *src_path = argv[1];
     char *dest_path = argv[2];
     int sleep_time = 300;
     int seconds = 0;
     int option;

     recursive = false;
     threshold = 10 * 1024 * 1024;

     while ((option = getopt(argc, argv, "t:Rs:")) != -1)
     {
          switch (option)
          {
          case 't':
               sleep_time = atoi(optarg);
               break;
          case 'R':
               recursive = true;
               break;
          case 's':
               threshold = atoi(optarg) * 1024 * 1024;
               break;
          }
     }

     printf("DAEMON ROZPOCZYNA PRACE \nKATALOG ZRODLOWY: %s, KATALOG DOCELOWY: %s\n", src_path, dest_path);
     printf("CZAS SPANIA: %d\n", sleep_time);
     printf("SYNCHRONIZACJA KATALOGOW: %s\n", recursive ? "TAK" : "NIE");
     printf("PROG ROZMIARU PLIKU (MB): %ld\n", threshold / (1024 * 1024));

     pid_t pid, sid;

     pid = fork();

     if (pid < 0)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Nie udalo sie utworzyc procesu demona");
          closelog();
          exit(1);
     }

     if (pid > 0)
     {
          printf("PID PROCESU: %d\n", pid);
          openlog("DAEMON_RUN", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Proces demona został uruchomiony");
          closelog();
          exit(0);
     }

     umask(0);

     sid = setsid();

     if (sid < 0)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Nie udalo sie utworzyc nowej sesji");
          closelog();
          exit(1);
     }

     if ((chdir("/")) < 0)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Nie udalo sie zmienic katalogu na root");
          closelog();
          exit(1);
     }

     close(STDIN_FILENO);
     close(STDOUT_FILENO);
     close(STDERR_FILENO);

     struct stat src_stat, dest_stat;

     while (1)
     {
          signal(SIGUSR1, signal_handler);

          if (seconds == sleep_time || forced_sync == true)
          {
               if (stat(src_path, &src_stat) == -1 || stat(dest_path, &dest_stat) == -1)
               {
                    openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Blad podczas pobierania informacji o sciezkach: %s; %s", src_path, dest_path);
                    closelog();
                    exit(0);
               }
               else if (S_ISDIR(src_stat.st_mode) == 0 || S_ISDIR(dest_stat.st_mode) == 0)
               {
                    openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Sciezki katalogow nie sa poprawne: %s; %s", src_path, dest_path);
                    closelog();
                    exit(0);
               }
               else if (S_ISDIR(src_stat.st_mode) == 1 && S_ISDIR(dest_stat.st_mode) == 1)
               {
                    openlog("DAEMON_WAKE", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Demon budzi sie");
                    closelog();

                    syncDirectory(src_path, dest_path);
               }

               seconds = 0;
               forced_sync = false;

               openlog("DAEMON_SLEEP", LOG_PID | LOG_CONS, LOG_USER);
               syslog(LOG_INFO, "Demon zasypia");
               closelog();
          }

          else
          {
               ++seconds;
          }

          sleep(1);
     }

     exit(0);
}