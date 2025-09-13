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

2) Em outro terminal, faça requisições ao servidor (exemplos):

```bash
# MYGET de um arquivo (ex.: README.md na pasta do servidor)
./client 127.0.0.1 README.md

# MYLASTACCESS (retorna instante do último acesso anterior na mesma conexão)
./client 127.0.0.1 LAST
```

Observações:
- O servidor escuta em `127.0.0.1:12345`.
- O caminho do arquivo em MYGET é resolvido em relação ao diretório atual do servidor.
- Para testar MYGET, garanta que o arquivo exista onde o servidor foi iniciado.

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
  - Cliente TCP simples: conecta em `<server-name>:12345`.
  - Uso:
    - `client <server-name> <file>` → envia `MYGET <file>` e imprime o corpo na stdout.
    - `client <server-name> LAST` → envia `MYLASTACCESS` e imprime a linha de resposta.
  - Valida e interpreta cabeçalhos: `OK <size>`, `ERR ...`, `LASTACCESS ...`.

### Testes rápidos sugeridos
1) Com o servidor rodando, crie/garanta um arquivo, p.ex. `README.md`.
2) Rode `./client 127.0.0.1 README.md` e verifique se o conteúdo impresso bate com o arquivo.
3) Rode `./client 127.0.0.1 LAST` duas vezes na mesma execução (cada chamada cria nova conexão):
   - Primeira vez tende a retornar `LASTACCESS NULL`.
   - Em sequência, após uma operação MYGET/erro na MESMA conexão, `LASTACCESS` refletiria o instante anterior. (Neste cliente, cada execução cria nova conexão, então use múltiplos comandos MYGET/ LAST em um cliente persistente para observar a mudança dentro da mesma conexão.)


