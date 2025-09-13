# 📄 Documentação – Execução de Server/Client em PCs diferentes via WSL

## 1. Cenário
- Dois computadores Windows, cada um rodando **WSL2** (Debian/Ubuntu).  
- O programa `server.c` roda em um dos PCs (servidor).  
- O programa `client.c` roda no outro PC (cliente).  
- Objetivo: permitir que o cliente acesse o servidor pela rede local.

---

## 2. Problema inicial
- O **IP do WSL** é interno (NAT) e muda a cada reinício → não pode ser acessado direto pela LAN.  
- O **IP do Windows** é o que os outros PCs enxergam.  
- Sem configuração extra, o cliente não conseguia falar com o servidor.

---

## 3. Solução aplicada (Opção B – Portproxy)

### 3.1. Conferir se o servidor escuta na porta correta
No WSL (PC servidor):
```bash
hostname -I                 # Descobrir IP interno do WSL
ss -ltnp | grep :12345      # Conferir que o server está ouvindo
```

### 3.2. Criar port forwarding no Windows

No PowerShell (Admin):

```bash
# Ver portproxies existentes
netsh interface portproxy show v4tov4

# Apagar regra antiga
netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=12345

# Criar nova regra apontando para o IP ATUAL do WSL
netsh interface portproxy add v4tov4 `
  listenaddress=0.0.0.0 listenport=12345 `
  connectaddress=<IP_WSL_ATUAL> connectport=12345
```

### 3.3. Liberar porta no Firewall

No Windows (PC servidor):

- Abrir Firewall do Windows com Segurança Avançada.

- Criar Nova Regra de Entrada → Porta TCP 12345 → Permitir conexão.

Ou via PowerShell:

```bash
New-NetFirewallRule -DisplayName "Servidor WSL 12345" `
  -Direction Inbound -Protocol TCP -LocalPort 12345 -Action Allow
```

---

## 4. Testes de conectividade

### 4.1. Do próprio Windows servidor

```bash
Test-NetConnection -ComputerName 192.168.x.y -Port 12345
```

Saída esperada: TcpTestSucceeded : True.


### 4.2. Do WSL cliente

```bash
# Teste TCP
nc -vz 192.168.x.y 12345

# Teste protocolo
printf "MYLASTACCESS\n" | nc 192.168.x.y 12345
```

---

## 5. Execução do Client

### 5.1. Compilar

No WSL cliente:

```bash
gcc client.c -o client
```

### 5.2. Rodar

```bash
./client 192.168.x.y LAST
```

ou

```bash
./client 192.168.x.y /caminho/para/arquivo
```

> Importante: sempre usar o IP do Windows do PC servidor, nunca o IP do WSL.

---

6. Armadilhas comuns

- O IP do WSL muda → recriar portproxy sempre que reiniciar o WSL.

- Test-NetConnection pode dar sucesso, mas se o connectaddress do portproxy estiver errado, o tráfego não chega no servidor.

- Se pedir arquivo inexistente → servidor responde ERR 404 NotFound.

- O cliente exige 2 argumentos: IP + comando (LAST ou caminho de arquivo).