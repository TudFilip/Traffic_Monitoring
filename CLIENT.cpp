#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <vector>
#include <string>
#include <iostream>
#include <climits>
#include <queue>
#include <time.h>
#include "sqlite3.h"

using namespace std;

//DEFINIREA PORTULUI
#define PORT 2909
//DEFINIREA UNEI VARIABILE CE RETINE NUMARUL DE ORASE(NODURI) DIN HARTA(GRAF)
#define ORASE 17

//FUNCTIA FOLOSITA DE THREADUL DIN PARCURGERE
static void *comenziClient(void *);

//DECLARARE VARIABILE GLOBALE
float hartaTrafic[20][20];                       //matricea folosita pentru a memora harta
vector<pair<unsigned int, unsigned int>> traseu; //vector ce retine drumurile din traseul unui client
int sd;                                          //descriptorul de socket

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
} client;

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

//---------- DEFINIREA FUNCTIILOR ----------

//FUNCTIA CE IMI INITIALIZEAZA MATRICEA CE RETINE HARTA PRIMITA DE LA SERVER
void initializareHartaTrafic()
{
    for (int i = 1; i <= ORASE; i++)
    {
        for (int j = 1; j <= ORASE; j++)
        {
            if (read(sd, &hartaTrafic[i][j], sizeof(float)) < 0)
            {
                perror("[client]Eroare la read() de la server matrice.\n");
                return;
            }
        }
    }
}

//VERIFIC DACA AM ORASUL TRIMIS CA PARAMETRU EXISTA
int verificareOras(const char *oras)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    const char *tmpOras;
    int orasExista = 0;
    int rc = sqlite3_open("BAZA_DATE.db", &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Nu am reusit deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_prepare_v2(db, "SELECT * from Orase;", -1, &stmt, 0);

    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        tmpOras = (const char *)sqlite3_column_text(stmt, 1);
        //cout<<tmpOras<<endl;
        if (strcmp(tmpOras, oras) == 0)
            orasExista = 1;
    }
    //sqlite3_finalize(stmt);
    sqlite3_close(db);

    return orasExista;
}

//RETURNEAZA ID-UL ORASULUI DAT CA PARAMETRU
unsigned int returnareID_Oras(const char *oras)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    const char *tmpOras;
    unsigned int ID = 0;
    int rc = sqlite3_open("BAZA_DATE.db", &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Nu am reusit deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_prepare_v2(db, "SELECT * from Orase;", -1, &stmt, 0);

    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        tmpOras = (const char *)sqlite3_column_text(stmt, 1);
        if (strcmp(tmpOras, oras) == 0)
            ID = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return ID;
}

//RETURNEAZA NUMELE UNUI ORAS DAT CA PARAMETRU PRIN ID-UL SAU
char *returnareNume_Oras(unsigned int ID)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    char *oras = (char *)malloc(100);
    unsigned int tmpID = 0;
    int rc = sqlite3_open("BAZA_DATE.db", &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Nu am reusit deschiderea bazei de date: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    rc = sqlite3_prepare_v2(db, "SELECT * from Orase;", -1, &stmt, 0);

    const char *var;
    char tmpOras[100] = "";
    while (sqlite3_step(stmt) != SQLITE_DONE)
    {
        var = (const char *)sqlite3_column_text(stmt, 1);
        tmpID = sqlite3_column_int(stmt, 0);
        if (tmpID == ID)
            strcat(tmpOras, var);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    int length = 0;
    for (size_t i = 0; i < strlen(tmpOras); i++)
    {
        oras[length++] = tmpOras[i];
    }
    oras[length] = '\0';

    return oras;
}

//FUNCTIA DE INITIALIZARE CLIENT
void initializareClient()
{
    char buffer[100];
    int ok_cerere_client = 0;
    while (!ok_cerere_client)
    {
        printf("Introduceti orasul de start: ");
        fgets(buffer, 100, stdin);
        buffer[strlen(buffer) - 1] = '\0';
        if (!verificareOras(buffer))
            printf("Orasul introdus nu exista! Introduceti altul existent!\n");
        else
        {
            ok_cerere_client = 1;
            client.orasPlecareClient_ID = returnareID_Oras(buffer);
        }
    }

    char orasPlecare[100] = "";
    strcpy(orasPlecare, buffer);

    ok_cerere_client = 0;
    while (!ok_cerere_client)
    {
        printf("Introduceti orasul de destinatie: ");
        fgets(buffer, 100, stdin);
        buffer[strlen(buffer) - 1] = '\0';
        if (!verificareOras(buffer))
            printf("Orasul introdus nu exista! Introduceti altul existent!\n");
        else if (strcmp(orasPlecare, buffer) == 0)
            printf("Nu puteti avea tot acelasi oras ca destinatie!\n");
        else
        {
            ok_cerere_client = 1;
            client.orasDestinatieClient_ID = returnareID_Oras(buffer);
        }
    }

    ok_cerere_client = 0;
    while (!ok_cerere_client)
    {
        printf("Introduceti viteza de start (<50): ");
        fgets(buffer, 100, stdin);
        buffer[strlen(buffer) - 1] = '\0';
        int viteza = atoi(buffer);
        if (viteza > 50)
            printf("Viteza este prea mare pentru a incepe! Introduceti o viteza mai mica!\n");
        else
        {
            ok_cerere_client = 1;
            client.vitezaClient = viteza;
        }
    }

    ok_cerere_client = 0;
    while (!ok_cerere_client)
    {
        printf("Doriti informatii despre preturile de la benzinarii, meteo si stiri? (da/nu): ");
        fgets(buffer, 100, stdin);
        buffer[strlen(buffer) - 1] = '\0';
        if (strcmp(buffer, "da") == 0)
        {
            client.informatii = true;
            ok_cerere_client = 1;
        }
        else if (strcmp(buffer, "nu") == 0)
        {
            client.informatii = false;
            ok_cerere_client = 1;
        }
        else if (strcmp(buffer, "nu") != 0 && strcmp(buffer, "da") != 0)
            printf("Comanda nu este corecta! Introduceti da sau nu!\n");
    }

    // cout << client.coordonataClient_X << endl;
    // cout << client.coordonataClient_Y << endl;
    // cout << client.idClient << endl;
    // cout << client.iesit << endl;
    // cout << client.informatii << endl;
    // cout << client.lungimeXY << endl;
    // cout << client.notificareAccident << endl;
    // cout << client.orasDestinatieClient_ID << endl;
    // cout << client.orasPlecareClient_ID << endl;
    // cout << client.timpParcurgere_XY << endl;
    // cout << client.vitezaClient << endl;

    unsigned int tmpValue;
    tmpValue = client.orasPlecareClient_ID;
    if (write(sd, &tmpValue, sizeof(unsigned int)) <= 0)
    {
        perror("[client]Eroare la write() spre server.\n");
        return;
    }

    tmpValue = client.orasDestinatieClient_ID;
    if (write(sd, &tmpValue, sizeof(unsigned int)) <= 0)
    {
        perror("[client]Eroare la write() spre server.\n");
        return;
    }

    tmpValue = client.vitezaClient;
    if (write(sd, &tmpValue, sizeof(unsigned int)) <= 0)
    {
        perror("[client]Eroare la write() spre server.\n");
        return;
    }

    bool tmpBool;
    tmpBool = client.informatii;
    if (write(sd, &tmpBool, sizeof(bool)) <= 0)
    {
        perror("[client]Eroare la write() spre server.\n");
        return;
    }

    if (read(sd, &client, sizeof(Client)) < 0)
    {
        perror("[client]Eroare la read() de la server AICI.\n");
        return;
    }
}

//CALCULUL TRASEULUI OPTIM DINTRE DOUA ORASE
//CODUL URMATOR E INSPIRAT DE PE https://www.geeksforgeeks.org/dijkstras-shortest-path-algorithm-greedy-algo-7/
vector<pair<int, int>> Lista[50];
int dist[50];
int tata[50];
bool viz[50];

void dijkstra(unsigned int start)
{
    int infinit = INT_MAX;

    struct Comp
    {
        bool operator()(int x, int y) { return dist[x] > dist[y]; }
    };

    priority_queue<int, vector<int>, Comp> q;

    for (int i = 1; i <= 20; i++)
        dist[i] = infinit; //initializam cu o val foarte mare
    dist[start] = 0;
    q.push(start);
    viz[start] = true;
    while (!q.empty())
    {
        int nod = q.top();
        q.pop();
        viz[nod] = false;
        for (long unsigned int i = 0; i < Lista[nod].size(); i++)
        {
            int vecin = Lista[nod][i].first;
            int cost = Lista[nod][i].second;
            if (dist[nod] + cost < dist[vecin])
            {
                dist[vecin] = dist[nod] + cost;
                if (viz[vecin] == false)
                {
                    q.push(vecin);
                    viz[vecin] = true;
                    tata[vecin] = nod;
                }
            }
        }
    }
}

vector<int> tmpTraseu;
void calculDrumVectorTati(unsigned int destinatie)
{
    if (tata[destinatie] == -1)
        return;
    calculDrumVectorTati(tata[destinatie]);
    tmpTraseu.emplace_back(destinatie);
}
//-----------------------------------------------------------------------

//CALCULAREA DRUMULUI PE CARE IL REALIZEAZA UN CLIENT
void calculareTraseu()
{
    for (int i = 1; i <= 17; i++)
    {
        for (int j = 1; j <= 17; j++)
        {
            if (hartaTrafic[i][j] != 0)
                Lista[i].push_back({j, hartaTrafic[i][j]});
        }
    }

    for (int i = 1; i <= 50; i++)
    {
        dist[i] = 0;
        tata[i] = 0;
        viz[i] = 0;
    }
    traseu.clear();
    tmpTraseu.clear();

    tata[client.orasPlecareClient_ID] = -1;
    dijkstra(client.orasPlecareClient_ID);
    tmpTraseu.emplace_back(client.orasPlecareClient_ID);
    calculDrumVectorTati(client.orasDestinatieClient_ID);

    for (long unsigned int i = 0; i < tmpTraseu.size() - 1; i++)
        traseu.push_back(make_pair(tmpTraseu[i], tmpTraseu[i + 1]));
}

//PRIMIREA INFORMATIILOR DE LA SERVER (PRETURI PECO, METEO SI NOUTATI)
int primireInformatii()
{
    size_t length;
    size_t i;

    //citim preturile
    char preturiPeco[200] = "";
    if (read(sd, &length, sizeof(size_t)) < 0)
    {
        perror("[client]Eroare la read() de la server.\n");
        return 1;
    }
    for (i = 0; i <= length; i++)
    {
        char c;
        if (read(sd, &c, sizeof(char)) < 0)
        {
            perror("[client]Eroare la read() de la server.\n");
            return 1;
        }
        preturiPeco[i] = c;
    }
    printf("\t-> PRETURI BENZINARII APROPIERE: %s\n", preturiPeco);

    //citim stirile
    char noutati[200] = "";
    if (read(sd, &length, sizeof(size_t)) < 0)
    {
        perror("[client]Eroare la read() de la server.\n");
        return 1;
    }
    for (i = 0; i <= length; i++)
    {
        char c;
        if (read(sd, &c, sizeof(char)) < 0)
        {
            perror("[client]Eroare la read() de la server.\n");
            return 1;
        }
        noutati[i] = c;
    }
    printf("\t-> ULTIMELE STIRI: %s\n", noutati);

    //citim vremea
    char meteo[200];
    if (read(sd, &length, sizeof(size_t)) < 0)
    {
        perror("[client]Eroare la read() de la server.\n");
        return 1;
    }
    for (i = 0; i <= length; i++)
    {
        char c;
        if (read(sd, &c, sizeof(char)) < 0)
        {
            perror("[client]Eroare la read() de la server.\n");
            return 1;
        }
        meteo[i] = c;
    }
    printf("\t-> VREMEA: %s\n", meteo);
}

//FUNCTIA CE GESTIONEAZA COMENZILE VENITE DE LA CLIENT
int vreauSaIes = 0;
void comenziClient()
{
    printf("Comanda dumneavoastra: ");
    fflush(stdout);
    char comanda[50];
    fgets(comanda, 50, stdin);
    comanda[strlen(comanda) - 1] = '\0';

    if (strcmp(comanda, "up") == 0)
        client.vitezaClient += 10;
    else if (strcmp(comanda, "down") == 0 && client.vitezaClient > 10)
        client.vitezaClient -= 10;
    else if (strcmp(comanda, "info_on") == 0)
        client.informatii = true;
    else if (strcmp(comanda, "info_off") == 0)
        client.informatii = false;
    else if (strcmp(comanda, "iesire") == 0)
        client.iesit = true, vreauSaIes = 1;
    else
        printf("Ati introdus o comanda gresita!\n");
}

//FUNCTIA CE SE OCUPA CU TRASEUL EFECTIV AL CLIENTULUI
int parcurgereTraseu()
{
    printf("---------------------------------------------------------------------\n");
    printf("COMENZI DINSPONIBILE:\n");
    printf("-> 'up' pentru a creste viteza actuala cu 10 km/h\n");
    printf("-> 'down' pentru a scade viteza actuala cu 10 km/h\n");
    printf("-> 'info_on' pentru permite primirea de informatii de la server\n");
    printf("-> 'info_off' pentru a opri primirea de informatii de la server\n");
    printf("-> 'iesire' pentru a iesi din aplicatie\n\n");
    printf("PARCURGEREA VA PORNI IMEDIAT...\n\n");

    for (const auto &drum : traseu)
    {
        if (client.orasDestinatieClient_ID == drum.second)
            client.iesit = true;

        client.coordonataClient_X = drum.first;
        client.coordonataClient_Y = drum.second;
        char *orasActual = returnareNume_Oras(client.coordonataClient_X);
        char *orasViitor = returnareNume_Oras(client.coordonataClient_Y);

        printf("\nORAS ACTUAL -> %s\n", orasActual);
        printf("ORAS VIITOR -> %s\n", orasViitor);
        printf("VITEZA ACTUALA -> %d\n", client.vitezaClient);
        printf("INFORMATII \n");

        free(orasActual);
        free(orasViitor);

        if (write(sd, &client, sizeof(Client)) <= 0)
        {
            perror("[client]Eroare la write() spre server.\n");
            return 1;
        }

        if (client.informatii)
            primireInformatii();
        else
            printf("\tNicio informatie de afisat!\n\n");

        //primesc timpul necesar parcurgerii acelui drum in functie de viteza mea actuala
        if (read(sd, &client.timpParcurgere_XY, sizeof(unsigned int)) < 0)
        {
            perror("[client]Eroare la read() de la server.\n");
            return 1;
        }

        printf("TIMP APROXIMAT PANA LA URMATORUL ORAS -> %u\n\n", client.timpParcurgere_XY);

        //primesc lungimea portiunii de drum pe care ma aflu
        if (read(sd, &client.lungimeXY, sizeof(unsigned int)) < 0)
        {
            perror("[client]Eroare la read() de la server.\n");
            return 1;
        }

        unsigned int tmpViteza = client.vitezaClient;
        if (client.timpParcurgere_XY == 0)
            client.timpParcurgere_XY++;
        comenziClient();
        bool vitezaCrescuta = false;
        if (client.vitezaClient > tmpViteza)
            vitezaCrescuta = true;

        if (write(sd, &vreauSaIes, sizeof(int)) <= 0)
        {
            perror("[client]Eroare la write() spre server.\n");
            return 1;
        }

        if (write(sd, &tmpViteza, sizeof(unsigned int)) <= 0)
        {
            perror("[client]Eroare la write() spre server.\n");
            return 1;
        }

        if (write(sd, &client, sizeof(Client)) <= 0)
        {
            perror("[client]Eroare la write() spre server.\n");
            return 1;
        }

        if (client.iesit == true && client.orasDestinatieClient_ID != drum.second)
        {
            printf("\n------- LA REVEDERE! -------\n");
            return 1;
        }

        if(vreauSaIes)
        {
            printf("\n------- LA REVEDERE! -------\n");
            return 1;
        }

        if (read(sd, &client, sizeof(Client)) < 0)
        {
            perror("[client]Eroare la read() de la server.\n");
            return 1;
        }

        if (client.vitezaClient > tmpViteza)
            printf("Viteza a crescut!\n");
        else if (client.vitezaClient == tmpViteza && vitezaCrescuta)
            printf("Ai depasit limita de viteza! Viteza nu mai poate creste!\n");
        else if (client.vitezaClient < tmpViteza)
            printf("Viteza a scazut!\n");

        printf("ASTEPTATI...\n");
        sleep(client.timpParcurgere_XY);
        
        if (client.orasDestinatieClient_ID == drum.second)
        {
            printf("AI AJUNS LA DESTINATIE!\n");
        }
    }

    int terminare = 0;
    char alegereContinuare[10];
    printf("\nDoresti sa continui calatoria in alt oras? (da/nu): ");
    fgets(alegereContinuare, 10, stdin);
    alegereContinuare[strlen(alegereContinuare) - 1] = '\0';

    if (strcmp(alegereContinuare, "nu") == 0)
    {
        printf("\n------- LA REVEDERE! -------\n");
        terminare = 1;
        if (write(sd, &terminare, sizeof(int)) <= 0)
        {
            perror("[client]Eroare la write() spre server.\n");
            return 1;
        }
    }
    else if (strcmp(alegereContinuare, "da") != 0 && strcmp(alegereContinuare, "nu") != 0)
        printf("Comanda nu este corecta! Introduceti da sau nu!\n");
    else
    {
        if (write(sd, &terminare, sizeof(int)) <= 0)
        {
            perror("[client]Eroare la write() spre server.\n");
            return 1;
        }

        client.iesit = false;
        client.orasPlecareClient_ID = client.orasDestinatieClient_ID;

        char buffer[100];
        int ok_cerere_client = 0;
        ok_cerere_client = 0;
        while (!ok_cerere_client)
        {
            printf("Introduceti orasul de destinatie: ");
            fgets(buffer, 100, stdin);
            buffer[strlen(buffer) - 1] = '\0';
            if (!verificareOras(buffer))
                printf("Orasul introdus nu exista! Introduceti altul existent!\n");
            // else if (strcmp(orasPlecare, buffer) == 0)
            //     printf("Nu puteti avea tot acelasi oras ca destinatie!\n");
            else
            {
                ok_cerere_client = 1;
                client.orasDestinatieClient_ID = returnareID_Oras(buffer);
            }
        }

        ok_cerere_client = 0;
        while (!ok_cerere_client)
        {
            printf("Introduceti viteza de start (<50): ");
            fgets(buffer, 100, stdin);
            buffer[strlen(buffer) - 1] = '\0';
            int viteza = atoi(buffer);
            if (viteza > 50)
                printf("Viteza este prea mare pentru a incepe! Introduceti o viteza mai mica!\n");
            else
            {
                ok_cerere_client = 1;
                client.vitezaClient = viteza;
            }
        }

        ok_cerere_client = 0;
        while (!ok_cerere_client)
        {
            printf("Doriti informatii despre preturile de la benzinarii, meteo si stiri? (da/nu): ");
            fgets(buffer, 100, stdin);
            buffer[strlen(buffer) - 1] = '\0';
            if (strcmp(buffer, "da") == 0)
            {
                client.informatii = true;
                ok_cerere_client = 1;
            }
            else if (strcmp(buffer, "nu") == 0)
            {
                client.informatii = false;
                ok_cerere_client = 1;
            }
            else if (strcmp(buffer, "nu") != 0 && strcmp(buffer, "da") != 0)
                printf("Comanda nu este corecta! Introduceti da sau nu!\n");
        }

        if (write(sd, &client, sizeof(Client)) <= 0)
        {
            perror("[client]Eroare la write() spre server.\n");
            return 1;
        }
        calculareTraseu();
        parcurgereTraseu();
        
    }
}

int main(int argc, char *argv[])
{
    struct sockaddr_in server; // structura folosita pentru conectare

    /* cream socketul */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Eroare la socket().\n");
        return errno;
    }

    /* umplem structura folosita pentru realizarea conexiunii cu serverul */
    /* familia socket-ului */
    server.sin_family = AF_INET;
    /* adresa IP a serverului */
    server.sin_addr.s_addr = INADDR_ANY;
    /* portul de conectare */
    server.sin_port = htons(PORT);

    /* ne conectam la server */
    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[client]Eroare la connect().\n");
        return errno;
    }

    initializareHartaTrafic();
    initializareClient();
    calculareTraseu();
    parcurgereTraseu();

    close(sd);
}