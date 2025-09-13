/* Cliente para protocolo MYGET/MYLASTACCESS - Modo Interativo
* Uso:
*   client <server-name>   -> modo interativo
*   Comandos disponíveis:
*     GET <file>           -> MYGET <file>\n
*     LAST                 -> MYLASTACCESS\n
*     QUIT                 -> encerra conexão
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

// Valores padroes, para a porta fixa do servidor e tamanho buffer leitura/escrita 
#define SERVER_PORT 12345
#define BUF_SIZE    4096

// A funcao de erro que se aplica no contexto que for 
static void fatal(const char *msg) {
    perror(msg);
    exit(1);
}

// Funcao que faz a leitura inteira do socket (para no \n)
static ssize_t recv_line(int fd, char *buf, size_t maxlen) {
    size_t i = 0;
    while (i + 1 < maxlen) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) { if (i == 0) return 0; break; }
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

// Funcaoo que le do socket numero fixo de bytes do socket (util para comparar com anterior)
static int read_n(int fd, void *buf, size_t n) {
    char *p = (char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t r = recv(fd, p, left, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return (int)(n - left); 
        left -= r;
        p += r;
    }
    return (int)n;
}

// Funcao que processa a resposta do servidor (extraida do main para reutilizacao no modo interativo)
static void process_response(int s) {
    char header[BUF_SIZE];
    ssize_t hl = recv_line(s, header, sizeof(header));
    if (hl <= 0) {
        fprintf(stderr, "Empty/closed response\n");
        return;
    }

    // Mostra o cabeçalho da resposta
    printf("Cabeçalho: %s\n", header);

    if (strncmp(header, "OK ", 3) == 0) {
        // Resposta "positiva", corpo com tamanho conhecido 
        long sz = 0;
        if (sscanf(header + 3, "%ld", &sz) != 1 || sz < 0) {
            fprintf(stderr, "Malformed OK header: %s", header);
            return;
        }

        // Esse eh o loop para ler e imprimir exatamente o numero de bytes
        char buf[BUF_SIZE];
        long left = sz;
        while (left > 0) {
            size_t chunk = (left > BUF_SIZE) ? BUF_SIZE : (size_t)left;
            int r = read_n(s, buf, chunk);
            if (r <= 0) {
                fprintf(stderr, "Truncated body (wanted %ld more bytes)\n", left);
                return;
            }
            if (write(1, buf, (size_t)r) < 0) fatal("write stdout"); //escrevendo na stdout
            left -= r;
        }
        printf("\n"); // Nova linha após o conteúdo do arquivo
    }
    else if (strncmp(header, "ERR ", 4) == 0) {
        // Resposta de erro do servidor (stderr) 
        fprintf(stderr, "%s", header);
    }
    else if (strncmp(header, "LASTACCESS ", 11) == 0) {
        // Resposta do ultimo acesso (stdout)
        if (write(1, header, (size_t)strlen(header)) < 0) fatal("write stdout");
    }
    else {
        // Resposta desconhecido (trata como erro)
        fprintf(stderr, "Unknown response: %s", header);
    }
}

// Funcao principal, interpreta os argumentos, conecta no servidor e processa comandos interativamente
int main(int argc, char **argv)
{
    int c, s;
    struct hostent *h;
    struct sockaddr_in channel;
    char input[BUF_SIZE];

    // Verificacao da passagem de argumentos (tem que ser 2 exato e o nome do programa)
    if (argc != 2) {
        fprintf(stderr, "Usage: %s server-name\n", argv[0]);
        fprintf(stderr, "Commands: GET <file>, LAST, QUIT\n");
        exit(1);
    }

    // Pegar nome do servidor 
    h = gethostbyname(argv[1]);
    if (!h) fatal("gethostbyname failed");

    // Criar o socket TCP
    s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) fatal("socket");

    // Preencher a struct de endereco do servidor
    memset(&channel, 0, sizeof(channel));
    channel.sin_family = AF_INET;
    memcpy(&channel.sin_addr.s_addr, h->h_addr, h->h_length);
    channel.sin_port = htons(SERVER_PORT);

    // Conectar ao servidor
    c = connect(s, (struct sockaddr *)&channel, sizeof(channel));
    if (c < 0) fatal("connect failed");

    printf("Conectado ao servidor. Comandos: GET <file>, LAST, QUIT\n");
    
    // Loop principal do modo interativo
    while (1) {
        printf("> ");
        fflush(stdout);
        
        // Ler comando do usuario
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break; // EOF
        }

        // Remove quebra de linha
        input[strcspn(input, "\n")] = '\0';

        if (strlen(input) == 0) continue;

        if (strcmp(input, "QUIT") == 0) {
            // Comando para encerrar conexao
            break;
        }
        else if (strncmp(input, "GET ", 4) == 0) {
            // Caso geral para todos outros acessos
            char line[BUF_SIZE];
            int n = snprintf(line, sizeof(line), "MYGET %s\n", input + 4);
            if (n < 0 || n >= (int)sizeof(line)) {
                fprintf(stderr, "Command too long\n");
                continue;
            }
            if (write(s, line, (size_t)n) < 0) {
                perror("write");
                break;
            }
            process_response(s);
        }
        else if (strcmp(input, "GET") == 0) {
            // Caso especial: GET sem argumentos (gera erro 400 no servidor)
            const char *msg = "MYGET \n";  // MYGET com espaço vazio
            if (write(s, msg, strlen(msg)) < 0) {
                perror("write");
                break;
            }
            process_response(s);
        }
        else if (strcmp(input, "LAST") == 0) {
            // Caso especial do ultimo acesso
            const char *msg = "MYLASTACCESS\n";
            if (write(s, msg, strlen(msg)) < 0) {
                perror("write");
                break;
            }
            process_response(s);
        }
        else {
            // Comando desconhecido
            printf("Comando desconhecido: %s\n", input);
            printf("Comandos disponíveis: GET <file>, LAST, QUIT\n");
        }
    }

    close(s);
    printf("Conexão encerrada.\n");
    return 0;
}
