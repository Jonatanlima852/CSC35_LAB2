/* Servidor com protocolo simples: MYGET e MYLASTACCESS
* - Concorrente (thread por conexão)
* - Estado por conexão: last_access
* - Respostas: OK <size>\n + corpo ; LASTACCESS <ISO8601>|NULL ; ERR <code> <msg>\n
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#define SERVER_PORT 12345    //  porta arbitrária onde irá rodar o processo. É importante configurá-la no client. 
#define BUF_SIZE    4096     //  tamanho do buffer para receber e enviar dados. Equivale a 4KB. 
// Não significa que não é possivel enviar algo maior, pois o protocolo faz o packet switching.

#define QUEUE_SIZE  10       // conexões de cliente aceitas 


// Função criada apenas para imprimir a msg de erro e finalizar o programa com status 1
static void fatal(const char *msg) {
    perror(msg);
    exit(1);
}

// Função para enviar todos os dados do buffer para o cliente
static ssize_t send_all(int fd, const void *buf, size_t len) {
    // converte o buffer para podermos manipular byte a byte
    const char *p = (const char *)buf;
    size_t left = len;  // mantém quantos bytes faltam para enviar

    // Loop até enviar todos os bytes
    while (left > 0) {
        ssize_t n = send(fd, p, left, 0);  // tenta enviar os bytes a partir da posição p do ponteiro
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return (ssize_t)(len - left); // atualiza quantos bytes faltam para enviar
        left -= n;
        p += n;
    }
    return (ssize_t)len; // retorna o número de bytes enviados
}

// função para receber uma linha do cliente
static ssize_t recv_line(int fd, char *buf, size_t maxlen) {
    size_t i = 0;

    // loop até atingir limite do buffer ou encontrar  '\n'
    while (i + 1 < maxlen) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0); // recebe um byte do cliente por vez
        if (n == 0) { // peer closed
            if (i == 0) return 0;
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0'; // termina a string com '\0'
    return (ssize_t)i;   // retorna o número de caracteres recebidos
}

// função para converter o tempo em formato ISO8601. Encontrada na internet.
static void iso8601_local(time_t t, char *out, size_t outlen) {
    if (t == 0) { snprintf(out, outlen, "NULL"); return; }
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, outlen, "%Y-%m-%dT%H:%M:%S%z", &tmv);
}

// estrutura para armazenar o file descriptor do cliente. será passada para cada thread
struct client_ctx {
    int fd;
};


// Função principal executada por cada thread
// será onde definiremos a implementação do nosso protocolo
static void *handle_client(void *arg) {
    struct client_ctx *ctx = (struct client_ctx *)arg;  // converte o argumento para o tipo da estrutura
    int sa = ctx->fd;  // armazena o file descriptor do cliente
    free(ctx);

    char line[BUF_SIZE];  // buffer para receber a linha do cliente
    char buf[BUF_SIZE];
    time_t last_access = 0; // estado por acesso, que vai guardar o ultimo acesso

    for (;;) {
        ssize_t ln = recv_line(sa, line, sizeof(line));
        if (ln <= 0) break; // fim da conexão ou erro

        // Remover \r?\n 
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r')) line[--ln] = '\0';

        // se a linha começa com "MYGET ", é uma requisição de arquivo
        if (strncmp(line, "MYGET ", 6) == 0) {
            const char *path = line + 6; // pega o caminho do arquivo

            // se o caminho é vazio, retorna erro 400
            if (*path == '\0') {
                const char *e = "ERR 400 BadRequest\n";
                if (send_all(sa, e, strlen(e)) < 0) break;
                last_access = time(NULL);
                continue;
            }

            // abre o arquivo
            int fd = open(path, O_RDONLY);

            // se o arquivo não foi encontrado, retorna erro 404
            if (fd < 0) {
                const char *e = "ERR 404 NotFound\n";
                if (send_all(sa, e, strlen(e)) < 0) break;
                last_access = time(NULL);
                continue;
            }

            // pega o tamanho do arquivo
            struct stat st;

            // se o arquivo não pode ser aberto, retorna erro 500
            if (fstat(fd, &st) < 0) {
                close(fd);
                const char *e = "ERR 500 Internal\n";
                if (send_all(sa, e, strlen(e)) < 0) break;
                last_access = time(NULL);
                continue;
            }

            // envia o cabeçalho "OK <tamanho do arquivo>"
            char hdr[128];
            snprintf(hdr, sizeof(hdr), "OK %ld\n", (long)st.st_size);
            if (send_all(sa, hdr, strlen(hdr)) < 0) { close(fd); break; }

            // Por fim, envia o arquivo para o cliente
            // Primeiro, faz-se a leitura e guarda no buffer. Depois, utiliza a função send_all para enviar o buffer para o cliente.
            for (;;) {
                ssize_t r = read(fd, buf, sizeof(buf));
                if (r < 0) {
                    if (errno == EINTR) continue;
                    /* erro de leitura de arquivo */
                    break;
                }
                if (r == 0) break; /* EOF */
                if (send_all(sa, buf, (size_t)r) < 0) { /* cliente caiu? */
                    break;
                }
            }
            close(fd);
            last_access = time(NULL);
        }
        // se a linha é "MYLASTACCESS", é uma requisição de último acesso
        else if (strcmp(line, "MYLASTACCESS") == 0) {
            char when[64];

            if (last_access == 0) {
                // se o último acesso é 0, retorna NULL
                const char *msg = "LASTACCESS NULL\n";
                if (send_all(sa, msg, strlen(msg)) < 0) break;
            } else {
                // se o último acesso é diferente de 0, retorna o tempo em formato ISO8601
                iso8601_local(last_access, when, sizeof(when));
                char out[128];
                snprintf(out, sizeof(out), "LASTACCESS %s\n", when);
                if (send_all(sa, out, strlen(out)) < 0) break;
            }
            // Atualiza após responder, pois a resposta pede o instante ANTERIOR
            last_access = time(NULL);
        }
        else {
            // se a linha não é "MYGET " ou "MYLASTACCESS", retorna erro 400 pois não está implementado no protocolo
            const char *e = "ERR 400 BadRequest\n";
            if (send_all(sa, e, strlen(e)) < 0) break;
            last_access = time(NULL);
        }
    }

    close(sa);
    return NULL;
}


int main(int argc, char *argv[]) {
    // s = descriptor do socket que será o servidor
    // b = resultado da bind 
    // l = resultado da listen
    // on = 1 - flag para reutilizar o endereço 
    int s, b, l, on = 1;

    // essa será a estrutura que armazenará o endereço do servidor(Ip e porta)
    struct sockaddr_in channel;

    // essa parte configura o programa para não considerar o sinal SIGPIPE,
    //  o que vai evitar que termine quando o cliente desconectar abruptamente
    signal(SIGPIPE, SIG_IGN); 

    // aqui, será zerada toda a estrutura channel para não haver lixo na memória
    memset(&channel, 0, sizeof(channel));

    // definimos que o socket usará o IPV4 
    channel.sin_family = AF_INET;

    // essas configuraçõse definem que o servidor aceitará conexões de qualuqer endereço IP
    // e a função htonl converte o host byte order para network byte order
    channel.sin_addr.s_addr = htonl(INADDR_ANY);

    // definindo a porta que o servidor irá escutar
    channel.sin_port = htons(SERVER_PORT);

    // aqui, tentamos criar um socket sob o protocolo TCP/IP. Caso falhe, chamamos a função fatal().
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) fatal("socket failed");

    // habilitando a reutilização do endereço e porta, assim o servidor pode ser reiniciado rapidamente sem o erro de "endereço já em uso"  
    // SO_REUSEADOR permite uso de endereço local
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
        fatal("setsockopt failed");

    // Diz-se ao kernel para associar o socket com um endereço e porta específicos definidos na struct channel
    b = bind(s, (struct sockaddr *)&channel, sizeof(channel));
    if (b < 0) fatal("bind failed");

    //  coloca o socket em modo de escuta, aceitando até 10 conexoes pendentes.
    l = listen(s, QUEUE_SIZE);
    if (l < 0) fatal("listen failed");

    // loop infinito para aceitar conexões
    for (;;) {
        // aceita uma nova conexão de cliente, e retorna um file descriptor sa
        //  obs: um file descriptor é um identificador único para o socket dado pelo sistema operacional.
        int sa = accept(s, 0, 0);  
        if (sa < 0) {
            if (errno == EINTR) continue;
            fatal("accept failed");
        }

        // declara variável para guardar o identificador da thread
        pthread_t th;

        // alocando a memória para a estrutura de contexto do cliente
        struct client_ctx *ctx = (struct client_ctx *)malloc(sizeof(*ctx));

        // se a alocação de memória falhar, fecha o socket do ciente e continua para a próxima conexão
        if (!ctx) { close(sa); continue; }

        // armazenando o file descriptor da conexão com o cliente na estrutura ctx
        ctx->fd = sa;

        // criando nova thread para este cliente e passando o contetxo
        // passamos a função handle_client e o contexto do cliente. 
        // a função handle_client é a função que será executada pela thread para cada cliente e será a nossa implementação de protocolo.
        if (pthread_create(&th, NULL, handle_client, ctx) != 0) {
            close(sa);
            free(ctx);
            continue;
        }

        // marca a thread como detached, assim o SO libera seus recursos autoamaticamente quando ela terminar
        pthread_detach(th);
    }
}
