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

// zmienna globalna recursive_sync - odpowiada za włączenie rekurencyjnej synchronizacji katalogów
// zmienna globalna forced_sync - odpowiada za wymuszone obudzenie demona
bool recursive_sync = false, forced_sync = false;

// zmienna globalna threshold - próg rozmiaru dla dużych plikow
long int threshold;

// obsługa sygnału SIGUSR1; funkcja ustawia zmienną forced_sync na true w przypadku otrzymania sygnału SIGUSR1
void signalHandler(int sig)
{
     // jeżeli otrzymany sygnał to sygnał SIGUSR1
     if (sig == SIGUSR1)
     {
          // wysłanie informacji o otrzymaniu sygnału do logu systemowego
          openlog("DAEMON_SIGNAL", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Otrzymano sygnal SIGUSR1");
          closelog();

          forced_sync = true;

          return;
     }
}

// funkcja copySmallFile - odpowiada za kopiowanie plików z rozmiarem poniżej progu; otrzymuje w argumentach ścieżkę źródłową oraz ścieżkę docelową
void copySmallFile(char *path_to_src, char *path_to_dest)
{
     // ustawienie bufora
     char buffer[1024];

     // deskryptory pliku źródłowego oraz docelowego
     int src_file;
     int dest_file;

     // zmienna przechowująca czytane bajty z pliku źródłowego
     int bytes;

     // otworzenie deskryptora pliku źródłowego
     if ((src_file = open(path_to_src, O_RDONLY)) == -1)
     {
          // wysłanie informacji o błędzie otwierania deskryptora pliku źródłowego do logu systemowego (jeżeli funkcja open() zwróciła -1)
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     // otworzenie deskryptora pliku docelowego
     if ((dest_file = open(path_to_dest, O_WRONLY | O_CREAT)) == -1)
     {
          // wysłanie informacji o błędzie otwierania deskryptora pliku docelowego do logu systemowego (jeżeli funkcja open() zwróciła -1)
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }

     // przepisanie danych z pliku źródłowego do pliku docelowego w pętli
     while ((bytes = read(src_file, buffer, sizeof(buffer))) > 0)
     {
          write(dest_file, buffer, bytes);
     }

     // zamknięcie deskryptora pliku źródłowego
     if (close(src_file) == -1)
     {
          // wysłanie informacji o błędzie zamykania pliku źródłowego do logu systemowego (jeżeli funkcja close() zwróciła -1)
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     // zamknięcie deskryptora pliku docelowego
     if (close(dest_file) == -1)
     {
          // wysłanie informacji o błędzie zamykania pliku docelowego do logu systemowego (jeżeli funkcja close() zwróciła -1)
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }

     // wysłanie informacji do logu systemowego o skopiowaniu pliku
     openlog("DAEMON_COPY", LOG_PID | LOG_CONS, LOG_USER);
     syslog(LOG_INFO, "Skopiowano maly plik %s do %s", path_to_src, path_to_dest);
     closelog();
     return;
}

// funkcja copyBigFile - odpowiada za kopiowanie plików z rozmiarem powyżej progu; otrzymuje w argumentach ścieżkę źródłową oraz ścieżkę docelową
void copyBigFile(char *path_to_src, char *path_to_dest)
{
     // zmienne na zamapowanie pliku źródłowego oraz docelowego do pamięci
     char *src_mmap, *dest_mmap;

     // deskryptor pliku źródłowego oraz docelowego
     int src_file;
     int dest_file;

     // zmienna przechowująca rozmiar pliku źródłowego
     size_t file_size;

     // otworzenie deskryptora pliku źródłowego
     src_file = open(path_to_src, O_RDONLY);

     // wysłanie informacji o błędzie otwierania deskryptora pliku źródłowego do logu systemowego (jeżeli funkcja open() zwróciła -1)
     if (src_file == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     // pobranie rozmiaru pliku źródłowego
     file_size = lseek(src_file, 0, SEEK_END);

     // wysłanie informacji o błędzie pobierania rozmiaru pliku źródłowego do logu systemowego (jeżeli funkcja lseek() zwróciła -1)
     if (file_size == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas pobierania rozmiaru pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     // zamapowanie pliku źródłowego do src_mmap
     src_mmap = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, src_file, 0);

     // wysłanie informacji o błędzie mapowania pliku źródłowego do logu systemowego (jeżeli funkcja mmap() zwróciła MAP_FAILED, czyli -1)
     if (src_mmap == MAP_FAILED)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas mapowania pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     // otworzenie deskryptora pliku docelowego
     dest_file = open(path_to_dest, O_RDWR | O_CREAT);

     // wysłanie informacji o błędzie otwierania deskryptora pliku docelowego do logu systemowego (jeżeli funkcja open() zwróciła -1)
     if (dest_file == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }

     // zmiana rozmiaru pliku docelowego na taką samą co plik źródłowy
     ftruncate(dest_file, file_size);

     // zamapowanie pliku docelowego do dest_mmap
     dest_mmap = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, dest_file, 0);

     // wysłanie informacji o błędzie mapowania pliku docelowego do logu systemowego (jeżeli funkcja mmap() zwróciła MAP_FAILED, czyli -1)
     if (dest_mmap == MAP_FAILED)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas mapowania pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }

     // skopiowanie zamapowanej pamięci ze źródła do celu
     if (memcpy(dest_mmap, src_mmap, file_size) == NULL)
     {
          // wysłanie informacji o błędzie kopiowania pamieci do logu systemowego (jeżeli memcpy() zwróciło NULL'a)
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas kopiowania pamieci z %s do %s", path_to_src, path_to_dest);
          closelog();
          return;
     }

     // zwolnienie pamięci zamapowanej do src_mmap
     if (munmap(src_mmap, file_size) == -1)
     {
          // wysłanie informacji o błędzie zwalniania pamięci pliku źródłowego do logu systemowego (jeżeli munmap() zwróciło -1)
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zwalniania pamieci dla pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     // zwolnienie pamięci zamapowanej do dest_mmap
     if (munmap(dest_mmap, file_size) == -1)
     {
          // wysłanie informacji o błędzie zwalniania pamięci pliku docelowego do logu systemowego (jeżeli munmap() zwróciło -1)
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zwalniania pamieci dla pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }

     // zamknięcie deskryptora pliku źródłowego
     if (close(src_file) == -1)
     {
          // wysłanie informacji o błędzie zamykania pliku źródłowego do logu systemowego (jeżeli funkcja close() zwróciła -1)
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania pliku zrodlowego: %s", path_to_src);
          closelog();
          return;
     }

     // zamknięcie deskryptora pliku docelowego
     if (close(dest_file) == -1)
     {
          // wysłanie informacji o błędzie zamykania pliku docelowego do logu systemowego (jeżeli funkcja close() zwróciła -1)
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania pliku docelowego: %s", path_to_dest);
          closelog();
          return;
     }

     // wysłanie informacji do logu systemowego o skopiowaniu pliku
     openlog("DAEMON_COPY", LOG_PID | LOG_CONS, LOG_USER);
     syslog(LOG_INFO, "Skopiowano duzy plik %s do %s", path_to_src, path_to_dest);
     closelog();
     return;
}

// funkcja clearDirectory(); czyści cały podkatalog w katalogu docelowym jeżeli nie ma go w katalogu źródłowym
void clearDirectory(char *path_to_dest)
{
     // wskaźnik na strukturę katalogu docelowego
     DIR *dest_dir;

     // utworzenie struktury danych typu dirent w celu przechowywania informacji o iterowanym pliku w katalogu
     struct dirent *dest_path_info;

     // otworzenie katalogu docelowego; jeżeli nie udało się otworzyć to informacja o błędzie jest wysłana do logu systemowego
     if ((dest_dir = opendir(path_to_dest)) == NULL)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania katalogu: %s", path_to_dest);
          closelog();
          return;
     }

     // pętla iterująca po zawartości w katalogu podanym w parametrze funkcji
     while ((dest_path_info = readdir(dest_dir)) != NULL)
     {
          // pominięcie katalogów specjalnych
          if (strcmp(dest_path_info->d_name, ".") == 0 || strcmp(dest_path_info->d_name, "..") == 0)
          {
               continue;
          }

          // ustawienie bufora na ścieżkę podkatalogu
          char sub_path[257];

          // wpisanie ścieżki podkatalogu do bufora
          snprintf(sub_path, sizeof(sub_path), "%s/%s", path_to_dest, dest_path_info->d_name);

          // jeżeli katalog
          if (dest_path_info->d_type == DT_DIR)
          {
               // wywołanie rekurencyjne funkcji dla podkatalogu
               clearDirectory(sub_path);

               // usunięcie pustego katalogu
               rmdir(sub_path);

               // wysłanie informacji do logu systemowego o usuniętym katalogu
               openlog("DAEMON_DELETE", LOG_PID | LOG_CONS, LOG_USER);
               syslog(LOG_INFO, "Usunieto katalog: %s", sub_path);
               closelog();
          }

          // jeżeli plik
          else if (dest_path_info->d_type == DT_REG)
          {
               // usunięcie tego pliku
               remove(sub_path);

               // wysłanie informacji do logu systemowego o usuniętym pliku
               openlog("DAEMON_DELETE", LOG_PID | LOG_CONS, LOG_USER);
               syslog(LOG_INFO, "Usunieto plik: %s", sub_path);
               closelog();
          }
     }
}

// funkcja syncDirectory(); synchronizuje dwa katalogi o ścieżkach podanych w argumentach tej funkcji
void syncDirectory(char *src_path, char *dest_path)
{
     // wskaźniki na strukturę katalogu źródłwego i docelowego
     DIR *src_dir, *dest_dir;

     // utworzenie struktur typu dirent w celu przechowywania informacji o zawartości w obu katalogach
     struct dirent *src_path_info, *dest_path_info;

     // otworzenie katalogu źródłowego; jeżeli zwróci NULL'a, to informacja o błędzie otwierania jest wysyłana do logu systemowego
     if ((src_dir = opendir(src_path)) == NULL)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania katalogu zrodlowego: %s", src_path);
          closelog();
          return;
     }

     // otworzenie katalogu docelowego; jeżeli zwróci NULL'a, to informacja o błędzie otwierania jest wysyłana do logu systemowego
     if ((dest_dir = opendir(dest_path)) == NULL)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas otwierania katalogu docelowego: %s", dest_path);
          closelog();
          return;
     }

     // wysłanie informacji do logu systemowego o sprawdzeniu poprawności katalogów
     openlog("DAEMON_CHECK_DIR", LOG_PID | LOG_CONS, LOG_USER);
     syslog(LOG_INFO, "Sprawdzono poprawnosc katalogow: %s; %s", src_path, dest_path);
     closelog();

     // pętla iterujaca po w zawartości w katalogu źródłowym
     while ((src_path_info = readdir(src_dir)) != NULL)
     {
          // utworzenie buforów przechowujacych ścieżki
          char path_to_src[257], path_to_dest[257];

          // wpisanie iterowanych ścieżek do buforów
          snprintf(path_to_src, sizeof(path_to_src), "%s/%s", src_path, src_path_info->d_name);
          snprintf(path_to_dest, sizeof(path_to_dest), "%s/%s", dest_path, src_path_info->d_name);

          // utworzenie struktur danych przechowujących dane o ścieżce źródłowej oraz docelowej
          struct stat src_path_stat, dest_path_stat;

          // pobranie danych ścieżek do struktur
          stat(path_to_src, &src_path_stat);
          stat(path_to_dest, &dest_path_stat);

          // jeżeli iterowana ścieżka jest katalogiem oraz rekurencyjna synchronizacja jest włączona
          if (src_path_info->d_type == DT_DIR && recursive_sync == true)
          {
               // jeżeli nie ma dostępu do katalogu docelowego (katalog nie istnieje)
               if (access(path_to_dest, F_OK) != 0)
               {
                    // utworzenie katalogu w katalogu docelowym
                    mkdir(path_to_dest, src_path_stat.st_mode);

                    // wysłanie informacji do logu systemowego o utworzeniu katalogu w kat. docelowym
                    openlog("DAEMON_MKDIR", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Utworzono nowy katalog w folderze docelowym: %s", path_to_dest);
                    closelog();
               }

               // jeżeli iterowana ścieżka nie jest katalogiem specjalnym
               if (strcmp(src_path_info->d_name, ".") != 0 && strcmp(src_path_info->d_name, "..") != 0)
               {
                    // wywołanie rekurencyjne funkcji syncDirectory() ze ścieżkami katalogów nadrzędnych
                    syncDirectory(path_to_src, path_to_dest);
               }
          }

          // jeżeli iterowana ścieżka jest plikiem
          else if (src_path_info->d_type == DT_REG)
          {
               // jeżeli jest dostęp do pliku źródłowego (plik istnieje), ale nie ma dostępu do pliku docelowego (plik nie istnieje)
               if (access(path_to_src, F_OK) == 0 && access(path_to_dest, F_OK) != 0)
               {
                    // jeżeli rozmiar danego pliku jest mniejszy od progu rozmiaru - kopiowanie małego pliku za pomocą copySmallFile()
                    if (src_path_stat.st_size < threshold)
                    {
                         copySmallFile(path_to_src, path_to_dest);
                    }

                    // jeżeli rozmiar danego pliku jest większy od progu rozmiaru - kopiowanie dużego pliku za pomocą copyBigFile()
                    else if (src_path_stat.st_size > threshold)
                    {
                         copyBigFile(path_to_src, path_to_dest);
                    }
               }

               // jeżeli jest dostęp do pliku źródłowego i do pliku docelowego (pliki istnieją)
               else if (access(path_to_src, F_OK) == 0 && access(path_to_dest, F_OK) == 0)
               {
                    // utworzenie zmiennych typu time_t, przechowujących datę modyfikacji plików
                    time_t src_file_mtime = src_path_stat.st_mtime, dest_file_mtime = dest_path_stat.st_mtime;

                    // jeżeli data modyfikacji pliku źródłowego jest późniejsza od daty modyfikacji pliku docelowego lub rozmiary plików są różne
                    if (difftime(src_file_mtime, dest_file_mtime) > 0 || src_path_stat.st_size != dest_path_stat.st_size)
                    {
                         // plik docelowy jest najpierw usuwany
                         remove(path_to_dest);

                         // teraz plik źródłowy jest przekopiowywany
                         // jeżeli rozmiar danego pliku jest mniejszy od progu rozmiaru - kopiowanie małego pliku za pomocą copySmallFile()
                         if (src_path_stat.st_size < threshold)
                         {
                              copySmallFile(path_to_src, path_to_dest);
                         }

                         // jeżeli rozmiar danego pliku jest większy od progu rozmiaru - kopiowanie dużego pliku za pomocą copyBigFile()
                         else if (src_path_stat.st_size > threshold)
                         {
                              copyBigFile(path_to_src, path_to_dest);
                         }
                    }
               }
          }
     }

     // pętla iterująca po zawartości w katalogu docelowym
     while ((dest_path_info = readdir(dest_dir)) != NULL)
     {
          // utworzenie buforów przechowujacych ścieżki
          char path_to_src[257], path_to_dest[257];

          // wpisanie iterowanych ścieżek do buforów
          snprintf(path_to_src, sizeof(path_to_src), "%s/%s", src_path, dest_path_info->d_name);
          snprintf(path_to_dest, sizeof(path_to_dest), "%s/%s", dest_path, dest_path_info->d_name);

          // jeżeli jest dostęp do ścieżki docelowej (istnieje), ale nie ma dostępu do ścieżki źródłowej (nie istnieje)
          if (access(path_to_dest, F_OK) == 0 && access(path_to_src, F_OK) != 0)
          {
               // jeżeli plik
               if (dest_path_info->d_type == DT_REG)
               {
                    // usunięcie pliku
                    remove(path_to_dest);

                    // wysłanie informacji do logu systemowego o usunięciu pliku docelowego
                    openlog("DAEMON_DELETE", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Usunieto plik: %s", path_to_dest);
                    closelog();
               }

               // jeżeli katalog
               else if (dest_path_info->d_type == DT_DIR && recursive_sync == true)
               {
                    // wyczyszczenie zawartości katalogu
                    clearDirectory(path_to_dest);

                    // usunięcie już pustego katalogu
                    rmdir(path_to_dest);

                    // wysłanie informacji do logu systemowego o usunięciu katalogu docelowego
                    openlog("DAEMON_DELETE", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Usunieto katalog: %s", path_to_dest);
                    closelog();
               }
          }
     }

     // zamknięcie katalogu źródłowego; jeżeli zwróci -1, to informacja o błedzie jest przesyłana do logu systemowego
     if (closedir(src_dir) == -1)
     {
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Blad podczas zamykania katalogu zrodlowego: %s", src_path);
          closelog();
          return;
     }

     // zamknięcie katalogu docelowego; jeżeli zwróci -1, to informacja o błedzie jest przesyłana do logu systemowego
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
     // zmienne przechowujące ścieżki podane w argumentach przy uruchamianiu demona
     char *src_path = argv[1];
     char *dest_path = argv[2];

     // zmienna przechowująca czas spania demona
     int sleep_time = 300;
     
     // zmienna do liczenia sekund
     int seconds = 0;

     // zmienna przechowująca ostatni argument z lini poleceń
     int option;

     // wyłączenie rekurencyjnej synchronizacji i ustawienie domślnego progu rozmiaru (20 MB) dla dużych plików
     recursive_sync = false;
     threshold = 20 * 1024 * 1024;

     //pętla przechodząca po podanych argumentach przy uruchamianiu
     while ((option = getopt(argc, argv, "t:Rs:")) != -1)
     {
          switch (option)
          {
          case 't':

               //ustawienie czasu spania
               sleep_time = atoi(optarg);

               break;

          case 'R':

               // włączenie rekurencyjnej synchronizacji katalogów
               recursive_sync = true;

               break;

          case 's':

               // ustawienie progu rozmiaru dużych plików (w MB)
               threshold = atoi(optarg) * 1024 * 1024;

               break;
          }
     }

     // informacje po uruchomieniu demona
     printf("Demon rozpoczyna prace \nKatalog zrodlowy: %s, Katalog docelowy: %s\n", src_path, dest_path);
     printf("- Czas spania: %ds\n", sleep_time);
     printf("- Rekurencyjna synchronizacja katalogow: %s\n", recursive_sync ? "Tak" : "Nie");
     printf("- Prog rozmiaru pliku (MB): %ld\n", threshold / (1024 * 1024));

     // zmienne przechowywujace id procesu i id sesji demona
     pid_t pid, sid;

     // utworzenie nowego procesu 
     pid = fork();

     // jeżeli id procesu jest mniejszy od 0
     if (pid < 0)
     {    
          // wysłanie informacji o błędzie tworzenia procesu do logu systemowego
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Nie udalo sie utworzyc procesu demona");
          closelog();
          exit(1);
     }

     // jeżeli id procesu jest większy od 0
     if (pid > 0)
     {    
          // udało się utworzyć proces demona 
          printf("Pid procesu: %d\n", pid);

          // informacja o powodzeniu tworzenia procesu jest wysłana do logu systemowego
          openlog("DAEMON_RUN", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Proces demona został uruchomiony");
          closelog();
          exit(0);
     }

     // nadanie wszystki praw dla tworzonych plików
     umask(0);

     // utworzenie nowej sesji
     sid = setsid();

     //jeżeli id sesji jest mniejsze od 0
     if (sid < 0)
     {    
          // wysłanie informacji do logu systemowego o niepowodzeniu w tworzeniu nowej sesji
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Nie udalo sie utworzyc nowej sesji");
          closelog();
          exit(1);
     }

     // zmiana katalogu na "root" 
     if ((chdir("/")) < 0)
     {    
          // wysłanie informacji do logu o niepowodzeniu zmiany katalogu na katalog "root" (jeżeli chdir() zwróci -1)
          openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
          syslog(LOG_INFO, "Nie udalo sie zmienic katalogu na root");
          closelog();
          exit(1);
     }

     // utworzenie struktur, które będą przechowywać informację o podanych na wejściu ścieżkach 
     struct stat src_stat, dest_stat;

     // włączenie obsługi sygnałów
     signal(SIGUSR1, signalHandler);

     // nieskończona pętla
     while (1)
     {    
          // jeżeli licznik jest równy czasowi spania lub wymuszono obudzenie demona
          if (seconds == sleep_time || forced_sync == true)
          {
               // wysłanie informacji o obudzeniu demona do logu systemowego
               openlog("DAEMON_WAKE", LOG_PID | LOG_CONS, LOG_USER);
               syslog(LOG_INFO, "Demon budzi sie");
               closelog();

               // próba pobrania informacji o ścieżkach
               if (stat(src_path, &src_stat) == -1 || stat(dest_path, &dest_stat) == -1)
               {
                    //wysłanie informacji do logu systemowego o błędzie pobierania informacji o ścieżkach
                    openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Blad podczas pobierania informacji o sciezkach: %s; %s", src_path, dest_path);
                    closelog();
                    exit(1);
               }

               // sprawdzenie, czy ścieżki to na pewno katalogi
               else if (S_ISDIR(src_stat.st_mode) == 0 || S_ISDIR(dest_stat.st_mode) == 0)
               {    
                    // wysłanie informacji do logu systemowego o tym, że podan ścieżki nie są katalogami
                    openlog("DAEMON_ERROR", LOG_PID | LOG_CONS, LOG_USER);
                    syslog(LOG_INFO, "Sciezki nie sa katalogami: %s; %s", src_path, dest_path);
                    closelog();
                    exit(1);
               }

               // jeżeli podane ścieżki to katalogi
               else if (S_ISDIR(src_stat.st_mode) == 1 && S_ISDIR(dest_stat.st_mode) == 1)
               {
                    // rozpoczęcie pracy demona
                    syncDirectory(src_path, dest_path);
               }

               // po zakończeniu pracy - ustawienie licznika sekund z powrotem na 0 oraz wyłączenie rekurencyjnej synchronizacji katalogów
               seconds = 0;
               forced_sync = false;

               // wysłanie informacji o uspaniu demona do logu systemowego
               openlog("DAEMON_SLEEP", LOG_PID | LOG_CONS, LOG_USER);
               syslog(LOG_INFO, "Demon zasypia");
               closelog();
          }
          
          // inkrementacja ilości sekund
          else
          {
               ++seconds;
          }

          // odczekanie sekundy do następnej iteracji
          sleep(1);
     }

     exit(0);
}
