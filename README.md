## Lab 2 — Servidor/Cliente TCP (MYGET / MYLASTACCESS)

### Compilação

```bash
gcc -pthread -o server server.c
gcc -o client client.c
```

### Execução rápida

1) Em um terminal, inicie o servidor:

```bash
./server
```

2) Em outro terminal, execute o cliente interativo:

```bash
# Modo interativo - permite múltiplas requisições na mesma conexão
./client localhost
```

3) Use os comandos disponíveis:

```bash
# Conectar ao servidor
./client localhost
Conectado ao servidor. Comandos: GET <file>, LAST, QUIT

# Fazer requisições
> GET teste.txt
Cabeçalho: OK 234
[conteúdo do arquivo será exibido]

> GET arquivo_inexistente.txt
Cabeçalho: ERR 404 NotFound

> GET
Cabeçalho: ERR 400 BadRequest

> LAST
Cabeçalho: LASTACCESS 2024-01-15T14:30:25-0300

> QUIT
Conexão encerrada.
```

### Modo não-interativo (cliente original)

Para compatibilidade, ainda é possível usar o cliente original:

```bash
# MYGET de um arquivo
./client 127.0.0.1 teste.txt

# MYLASTACCESS (cada execução cria nova conexão)
./client 127.0.0.1 LAST
```

Observações:
- O servidor escuta em `127.0.0.1:12345`.
- O caminho do arquivo em MYGET é resolvido em relação ao diretório atual do servidor.
- O modo interativo mantém a conexão aberta, permitindo que LAST funcione corretamente.
- Cada comando mostra o cabeçalho da resposta para debug.

### Protocolo suportado
- `MYGET <path>\n` → resposta `OK <size>\n` seguida de `<size>` bytes do arquivo, ou `ERR 404 NotFound\n`, `ERR 400 BadRequest\n`, `ERR 500 Internal\n`.
- `MYLASTACCESS\n` → resposta `LASTACCESS <ISO8601>\n` ou `LASTACCESS NULL\n`.

### Sobre os códigos

- server.c
  - Servidor TCP concorrente (uma thread por conexão) na porta 12345.
  - Trata linhas de comando do cliente: `MYGET` e `MYLASTACCESS`.
  - Em `MYGET`, envia cabeçalho `OK <size>` e o conteúdo do arquivo em blocos.
  - Em `MYLASTACCESS`, devolve o instante do último acesso anterior na MESMA conexão (estado por conexão) em formato ISO8601, ou `NULL` se ainda não houve.
  - Responde erros com `ERR <código> <mensagem>` e ignora `SIGPIPE`.

- client.c
  - Cliente TCP interativo: conecta em `<server-name>:12345` e mantém conexão aberta.
  - Uso:
    - `client <server-name>` → modo interativo com comandos GET, LAST, QUIT.
    - `client <server-name> <file>` → modo não-interativo (compatibilidade).
    - `client <server-name> LAST` → modo não-interativo (compatibilidade).
  - Comandos interativos:
    - `GET <file>` → envia `MYGET <file>` e exibe conteúdo + cabeçalho.
    - `GET` → envia `MYGET ` (sem argumentos) para gerar erro 400.
    - `LAST` → envia `MYLASTACCESS` e exibe resposta + cabeçalho.
    - `QUIT` → encerra conexão.
  - Valida e interpreta cabeçalhos: `OK <size>`, `ERR ...`, `LASTACCESS ...`.
  - Exibe cabeçalhos das respostas para facilitar debug.

### Testes rápidos sugeridos
1) Com o servidor rodando, use o arquivo `teste.txt` incluído.
2) Rode `./client localhost` e teste:
   - `GET teste.txt` → deve exibir o conteúdo do arquivo.
   - `GET arquivo_inexistente.txt` → deve retornar erro 404.
   - `GET` → deve retornar erro 400.
   - `LAST` → deve retornar NULL na primeira vez, depois o timestamp do último acesso.
   - `QUIT` → deve encerrar a conexão.


