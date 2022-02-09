#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <vector>
#include <string>
#include <iostream>
#include "sqlite3.h"

using namespace std;

//DEFINIREA PORTULUI
#define PORT 2909
//DEFINIREA NUMARULUI DE THREAD-URI CREATE
#define NTHREADS 5
//DEFINIREA NUMARULUI DE CONEXIUNI LA SERVER AFLATE IN ASTEPTARE
#define BACKLOG 5
//DEFINIREA UNEI VARIABILE CE RETINE NUMARUL DE ORASE(NODURI) DIN HARTA(GRAF)
#define ORASE 17
//DEFINIREA FISIERULUI CU BAZA DE DATE
#define DATABASE "BAZA_DATE.db"

//FUNCTIA FOLOSITA DE THREAD
static void *treat(void *);

//DECLARARE VARIABILE GLOBALE
float hartaTrafic[20][20]; //matricea folosita pentru a memora harta
int nrClienti = 0;         //numarul de clienti conectati pe server la un moment dat
int sd;                    //descriptorul de socket de ascultare

//DEFINIREA STRUCTURILOR
struct Client
{
    unsigned int idClient = 0;
    unsigned int orasPlecareClient_ID = 0;
    unsigned int orasDestinatieClient_ID = 0;
    unsigned int coordonataClient_X = 0;
    unsigned int coordonataClient_Y = 0;
    unsigned int timpParcurgere_XY = 0;
    unsigned int lungimeXY = 0;
    unsigned int vitezaClient = 0;
    bool notificareAccident = false;
    bool informatii = false;
    bool iesit = false;
};
vector<Client> clienti(20);

struct Aglomeratie
{
    unsigned int coordonataAglomeratie_X;
    unsigned int coordonataAglomeratie_Y;
};
vector<Aglomeratie> aglomeratii;

struct Accident
{
    unsigned int coordonataAccident_X;
    unsigned int coordonataAccident_Y;
    unsigned int timpRamasAccident = 50;
};
vector<Accident> accidente;

//UN ARRAY DE THREAD-URI PENTRU CREAREA UNUI NUMAR DE THREAD-URI DORIT
pthread_t *threadsPool;
//VARIABILA MUTEX CARE VA FI FOLOSITA DE THREAD-URI
pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER;

//---------- DEFINIREA FUNCTIILOR ----------

//CREAREA MATRICEI DE ADIACENTA A GRAFULUI (HARTII) CU VALORILE DE TIMP CORESPUNZATOARE
void matriceHarta()
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int index_linie, index_coloana, distanta;
    const char *tip_drum;
    int rc = sqlite3_open(DATABASE, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Nu am reusit deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_prepare_v2(db, "SELECT * from Drumuri;", -1, &stmt, 0);

    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        index_linie = sqlite3_column_int(stmt, 2);
        index_coloana = sqlite3_column_int(stmt, 3);
        tip_drum = (const char *)sqlite3_column_text(stmt, 1);
        distanta = sqlite3_column_int(stmt, 4);
        hartaTrafic[index_linie][index_coloana] = distanta / (float)((strcmp(tip_drum, "national") == 0) ? 100 : 130);
        hartaTrafic[index_coloana][index_linie] = distanta / (float)((strcmp(tip_drum, "national") == 0) ? 100 : 130);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

//TRIMITEREA CATRE CLIENT A MATRICEI CREATE
void transmitereMatriceHarta(int cl, int idThread)
{
    for (int i = 1; i <= 17; i++)
    {
        for (int j = 1; j <= 17; j++)
        {
            if (write(cl, &hartaTrafic[i][j], sizeof(float)) <= 0)
            {
                printf("[Thread %d]\n", idThread);
                perror("Eroare la write() catre client.\n");
            }
        }
    }
    printf("[Thread %d]Matricea a fost trasmisa cu succes.\n", idThread);
}

//PREIA PRETURILE LA PECO DE PE UN DRUM ANUME AFLAT INTRE DOUA ORASE
char *preluarePreturiPeco(unsigned int oras_X, unsigned int oras_Y)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    char *info = (char *)malloc(254);
    char tmpInfo[254] = "";
    int rc = sqlite3_open(DATABASE, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Nu am reusit deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_prepare_v2(db, "SELECT * from Peco;", -1, &stmt, 0);

    unsigned int tmpX, tmpY, ben = 0, mot = 0;
    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        tmpX = sqlite3_column_int(stmt, 1);
        tmpY = sqlite3_column_int(stmt, 2);
        if ((tmpX == oras_X && tmpY == oras_Y) || (tmpX == oras_Y && tmpY == oras_X))
        {
            ben = sqlite3_column_int(stmt, 3);
            mot = sqlite3_column_int(stmt, 4);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (mot == 0 && ben == 0)
        strcat(tmpInfo, "Din pacate nu exista nicio benzinarie pe drum!");
    else
        sprintf(tmpInfo, "Motorina - %d RON\tBenzina - %d RON", mot, ben);

    int length = 0;
    size_t i;
    for (i = 0; i < strlen(tmpInfo); i++)
        info[length++] = tmpInfo[i];
    info[length] = '\0';

    return info;
}

//PREIA STIRILE INTR UN MOD ALEATORIU PENTRU A FI TRANSMISE CATRE CLIENT
char *preluareNoutati()
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    char *info = (char *)malloc(254);
    int rc = sqlite3_open(DATABASE, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Nu am reusit deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_prepare_v2(db, "SELECT * from Stiri;", -1, &stmt, 0);

    char textStire[200] = "";
    const char *stire;
    const char *canal;
    srand(time(NULL));
    int idStire = (rand() % 10) + 1;
    int tmpID;
    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        tmpID = sqlite3_column_int(stmt, 0);
        if (tmpID == idStire)
        {
            stire = (const char *)sqlite3_column_text(stmt, 1);
            canal = (const char *)sqlite3_column_text(stmt, 2);
            strcat(textStire, stire);
            strcat(textStire, ", Canal: ");
            strcat(textStire, canal);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    int length = 0;
    size_t i;
    for (i = 0; i < strlen(textStire); i++)
        info[length++] = textStire[i];
    info[length] = '\0';

    return info;
}

//PREIA REGIUNEA UNUI ORAS DAT CA ID
char *preluareRegiune(unsigned int orasID)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    char *regiune = (char *)malloc(100);
    int rc = sqlite3_open(DATABASE, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Nu am reusit deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_prepare_v2(db, "SELECT * from Orase;", -1, &stmt, 0);

    unsigned int idOras;
    const char *reg;
    char tmp[100] = "";
    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        idOras = sqlite3_column_int(stmt, 0);
        if (idOras == orasID)
        {
            reg = (const char *)sqlite3_column_text(stmt, 2);
            strcat(tmp, reg);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    int length = 0;
    size_t i;
    for (i = 0; i < strlen(tmp); i++)
        regiune[length++] = tmp[i];
    regiune[length] = '\0';

    return regiune;
}

//PREIA VREMEA PENTRU UN ANUMIT ORAS DAR PRIN ID
char *preluareMeteo(unsigned int oras)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    char *info = (char *)malloc(254);
    int rc = sqlite3_open(DATABASE, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Nu am reusit deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_prepare_v2(db, "SELECT * from Meteo;", -1, &stmt, 0);

    char *regiune = preluareRegiune(oras);
    const char *tmpRegio;
    int ora, prognoza;
    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        tmpRegio = (const char *)sqlite3_column_text(stmt, 1);
        if (strcmp(regiune, tmpRegio) == 0)
        {
            ora = sqlite3_column_int(stmt, 2);
            prognoza = sqlite3_column_int(stmt, 3);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    char textMeteo[100] = "";
    sprintf(textMeteo, "In %s sunt %d grade la ora %d", regiune, prognoza, ora);
    free(regiune);

    int length = 0;
    size_t i;
    for (i = 0; i < strlen(textMeteo); i++)
        info[length++] = textMeteo[i];
    info[length] = '\0';

    return info;
}

//TRIMITE CATRE CLIENT INFORMATII LEGATE DE VREME, STIRI SI PRETURI LA BENZINARII
void trimitereInformatii(int cl, unsigned int oras_X, unsigned int oras_Y)
{
    size_t length;
    size_t i;

    char *preturiPeco = preluarePreturiPeco(oras_X, oras_Y);
    length = strlen(preturiPeco);
    if (write(cl, &length, sizeof(size_t)) <= 0)
    {
        perror("[Thread]Eroare la write() catre client.\n");
        close(cl);
    }
    for (i = 0; i <= length; i++)
    {
        char c = preturiPeco[i];
        if (write(cl, &c, sizeof(char)) <= 0)
        {
            perror("[Thread]Eroare la write() catre client.\n");
            close(cl);
        }
    }
    free(preturiPeco);

    char *noutati = preluareNoutati();
    length = strlen(noutati);
    if (write(cl, &length, sizeof(size_t)) <= 0)
    {
        perror("[Thread]Eroare la write() catre client.\n");
        close(cl);
    }
    for (i = 0; i <= length; i++)
    {
        char c = noutati[i];
        if (write(cl, &c, sizeof(char)) <= 0)
        {
            perror("[Thread]Eroare la write() catre client.\n");
            close(cl);
        }
    }
    free(noutati);

    char *meteo = preluareMeteo(oras_Y);
    length = strlen(meteo);
    if (write(cl, &length, sizeof(size_t)) <= 0)
    {
        perror("[Thread]Eroare la write() catre client.\n");
        close(cl);
    }
    for (i = 0; i <= length; i++)
    {
        char c = meteo[i];
        if (write(cl, &c, sizeof(char)) <= 0)
        {
            perror("[Thread]Eroare la write() catre client.\n");
            close(cl);
        }
    }
    free(meteo);
}

//CALCULEAZA TIMPUL NECESAR PARCURGERII UNEI PORTIUNI DE DRUM IN FUNCTIE DE VITEZA ACTUALA
void calculTimpParcurgereDrumXY(int index)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc = sqlite3_open(DATABASE, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Nu am reusit deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_prepare_v2(db, "SELECT * from Drumuri;", -1, &stmt, 0);

    unsigned int lungimeDrumXY;
    unsigned int orasX, orasY;
    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        orasX = sqlite3_column_int(stmt, 2);
        orasY = sqlite3_column_int(stmt, 3);
        if ((orasX == clienti[index].coordonataClient_X && orasY == clienti[index].coordonataClient_Y) ||
            (orasX == clienti[index].coordonataClient_Y && orasY == clienti[index].coordonataClient_X))
            lungimeDrumXY = sqlite3_column_int(stmt, 4);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    clienti[index].lungimeXY = lungimeDrumXY;
    clienti[index].timpParcurgere_XY = (unsigned int)((lungimeDrumXY / clienti[index].vitezaClient) * 5);
}

//CAUTA IN BAZA DE DATE VITEZA MAXIMA ADMISA PE ACEA PORTIUNE DE DRUM
int returnareVitezaMaximaAdmisa(unsigned int Oras_X, unsigned int Oras_Y)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc = sqlite3_open(DATABASE, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Nu am reusit deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_prepare_v2(db, "SELECT * from Drumuri;", -1, &stmt, 0);

    const char *tip_drum;
    unsigned int orasX, orasY;
    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        orasX = sqlite3_column_int(stmt, 2);
        orasY = sqlite3_column_int(stmt, 3);
        if ((orasX == Oras_X && orasY == Oras_Y) || (orasX == Oras_Y && orasY == Oras_X))
            tip_drum = (const char *)sqlite3_column_text(stmt, 1);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (strcmp(tip_drum, "national") == 0)
        return 100;
    else
        return 130;
}

//REINITIALIZARE POZITIE DIN VECTORUL CLIENTI UNDE A FOST UN CLIENT
void reinitializareClient(int index)
{
    clienti[index].idClient = 0;
    clienti[index].orasPlecareClient_ID = 0;
    clienti[index].orasDestinatieClient_ID = 0;
    clienti[index].coordonataClient_X = 0;
    clienti[index].coordonataClient_Y = 0;
    clienti[index].timpParcurgere_XY = 0;
    clienti[index].lungimeXY = 0;
    clienti[index].vitezaClient = 0;
    clienti[index].notificareAccident = false;
    clienti[index].informatii = false;
    clienti[index].iesit = false;
}

//FUNCTIA DE BAZA PE CARE O EXECUTA THREAD-URILE
void *treat(void *arg) /* functia executata de fiecare thread ce realizeaza comunicarea cu clientii */
{
    int client;
    struct sockaddr_in from;
    bzero(&from, sizeof(from));
    printf("[thread]- %d - pornit...\n", (int &)arg);
    fflush(stdout);

    for (;;)
    {
        socklen_t length = sizeof(from);
        pthread_mutex_lock(&mlock);
        //printf("Thread %d trezit\n",(int)arg);
        if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
        {
            perror("[thread]Eroare la accept().\n");
        }
        transmitereMatriceHarta(client, (int &)arg);

        int indexClienti;
        indexClienti = nrClienti;
        nrClienti++;
        pthread_mutex_unlock(&mlock);

        unsigned int tmpValue;
        if (read(client, &tmpValue, sizeof(unsigned int)) <= 0)
        {
            perror("Eroare la read() de la client.\n");
            nrClienti--;
            close(client);
        }
        else
            perror("Am facut readul: ");
        clienti[indexClienti].orasPlecareClient_ID = tmpValue;

        if (read(client, &tmpValue, sizeof(unsigned int)) <= 0)
        {
            perror("Eroare la read() de la client.\n");
            nrClienti--;
            close(client);
        }
        else
            perror("Am facut readul: ");
        clienti[indexClienti].orasDestinatieClient_ID = tmpValue;

        if (read(client, &tmpValue, sizeof(unsigned int)) <= 0)
        {
            perror("Eroare la read() de la client.\n");
            nrClienti--;
            close(client);
        }
        else
            perror("Am facut readul: ");
        clienti[indexClienti].vitezaClient = tmpValue;

        bool tmpBool;
        if (read(client, &tmpBool, sizeof(bool)) <= 0)
        {
            perror("Eroare la read() de la client.\n");
            nrClienti--;
            close(client);
        }
        else
            perror("Am facut readul: ");
        clienti[indexClienti].informatii = tmpBool;

        clienti[indexClienti].idClient = nrClienti;

        if (write(client, &clienti[indexClienti], sizeof(Client)) <= 0)
        {
            perror("[Thread]Eroare la write() catre client.\n");
            exit(1);
        }
        else
            perror("Am facut write ul");

        printf("Incepem parcurgerea de la clientul cu indexul %d\n", indexClienti);
        //aici incepe clientul traseul lui
        bool vreauSaIes = false;
        int var = 0;
        while (clienti[indexClienti].iesit == false && var == 0)
        {
            //primesc datele clientului la fiecare iteratie a traseului
            if (read(client, &clienti[indexClienti], sizeof(Client)) <= 0)
            {
                perror("Eroare la read() de la client.\n");
                nrClienti--;
                close(client);
            }

            if (clienti[indexClienti].informatii)
                trimitereInformatii(client, clienti[indexClienti].coordonataClient_X, clienti[indexClienti].coordonataClient_Y);

            calculTimpParcurgereDrumXY(indexClienti);

            //trimit timpul pe care il are un client de parcurs pe o portiune de drum in functie de viteza sa actuala
            //timpul este rotunjit la cel mai apropiat intreg

            if (write(client, &clienti[indexClienti].timpParcurgere_XY, sizeof(unsigned int)) <= 0)
            {
                perror("[Thread]Eroare la write() catre client.\n");
                nrClienti--;
                close(client);
            }

            if (write(client, &clienti[indexClienti].lungimeXY, sizeof(unsigned int)) <= 0)
            {
                perror("[Thread]Eroare la write() catre client.\n");
                nrClienti--;
                close(client);
            }

            cout << "Timp ramas de asteptat: " << clienti[indexClienti].timpParcurgere_XY << endl;

            //---------------
            
            if (read(client, &var, sizeof(int)) <= 0)
            {
                perror("Eroare la read() de la client.\n");
                nrClienti--;
                close(client);
            }

            unsigned int tmpViteza;
            if (read(client, &tmpViteza, sizeof(unsigned int)) <= 0)
            {
                perror("Eroare la read() de la client.\n");
                nrClienti--;
                close(client);
            }

            if (read(client, &clienti[indexClienti], sizeof(Client)) <= 0)
            {
                perror("Eroare la read() de la client.\n");
                nrClienti--;
                close(client);
            }

            if (clienti[indexClienti].iesit == true && var == 1)
            {
                vreauSaIes = true;
                break;
            }

            cout << "sunt aici" << endl;
            int maxVitezaAdmisa = returnareVitezaMaximaAdmisa(clienti[indexClienti].coordonataClient_X, clienti[indexClienti].coordonataClient_Y);
            cout << "viteza max: " << maxVitezaAdmisa << endl;

            if (clienti[indexClienti].vitezaClient > maxVitezaAdmisa)
            {
                clienti[indexClienti].vitezaClient -= 10;
                clienti[indexClienti].timpParcurgere_XY--;
            }
            if (clienti[indexClienti].vitezaClient == tmpViteza)
                clienti[indexClienti].timpParcurgere_XY--;
            else
            {
                clienti[indexClienti].timpParcurgere_XY = (unsigned int)((clienti[indexClienti].lungimeXY / clienti[indexClienti].vitezaClient) * 5);
                //clienti[indexClienti].timpParcurgere_XY--;
            }
            cout << "Timp ramas de asteptat: " << clienti[indexClienti].timpParcurgere_XY << endl;
            sleep(1);

            if (write(client, &clienti[indexClienti], sizeof(Client)) <= 0)
            {
                perror("[Thread]Eroare la write() catre client.\n");
                nrClienti--;
                close(client);
            }
            //-----------------
            cout << "viteza mea este de: " << clienti[indexClienti].vitezaClient << endl;
            if (vreauSaIes)
                break;

            if (clienti[indexClienti].iesit == true && var == 0)
            {
                int terminare;
                if (read(client, &terminare, sizeof(int)) <= 0)
                {
                    perror("Eroare la read() de la client.\n");
                    nrClienti--;
                    close(client);
                }

                if (terminare)
                    break;
                else
                {
                    if (read(client, &clienti[indexClienti], sizeof(Client)) <= 0)
                    {
                        perror("Eroare la read() de la client.\n");
                        nrClienti--;
                        close(client);
                    }
                }
            }
        }
        reinitializareClient(indexClienti);
        nrClienti--;
        /* am terminat cu acest client, inchidem conexiunea */
        printf("Am inchis conexiunea!\n");
        close(client);
    }
}

void threadCreate(int i)
{
    pthread_create(&threadsPool[i], NULL, &treat, (void *)i);
}

int main(int argc, char *argv[])
{
    matriceHarta();

    struct sockaddr_in server; // structura folosita de server

    threadsPool = (pthread_t *)calloc(sizeof(pthread_t), NTHREADS);

    /* crearea unui socket */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("[server]Eroare la socket().\n");
        return errno;
    }

    /* utilizarea optiunii SO_REUSEADDR */
    int on = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    /* pregatirea structurilor de date */
    bzero(&server, sizeof(server));

    /* umplem structura folosita de server */
    /* stabilirea familiei de socket-uri */
    server.sin_family = AF_INET;
    /* acceptam orice adresa */
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    /* utilizam un port utilizator */
    server.sin_port = htons(PORT);

    /* atasam socketul */
    if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[server]Eroare la bind().\n");
        return errno;
    }

    /* punem serverul sa asculte daca vin clienti sa se conecteze */
    if (listen(sd, BACKLOG) == -1)
    {
        perror("[server]Eroare la listen().\n");
        return errno;
    }

    printf("Nr threaduri %d \n", NTHREADS);
    fflush(stdout);

    // cream threadurile la care se vor conecta clientii
    int i;
    for (i = 0; i < NTHREADS; i++)
        threadCreate(i);

    /* servim in mod concurent clientii...folosind thread-uri */
    for (;;)
    {
        printf("[server]Asteptam la portul %d...\n", PORT);
        pause();
    }
}
