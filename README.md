# GoLive P2P

Aplicativo desktop para compartilhar tela com até cinco pessoas na mesma tailnet Tailscale. O host coordena a sala por WebSocket no próprio IP Tailscale; vídeo e áudio trafegam por uma conexão WebRTC independente para cada espectador. Não há servidor central de mídia.

## Plataformas e requisitos

- Windows 10/11: executável portátil, plataforma principal.
- Linux: AppImage. A captura de tela depende do compositor e, em Wayland, do xdg-desktop-portal.
- Tailscale instalado, autenticado e conectado à mesma tailnet em cada computador.
- Para desenvolvimento: Node.js 22+ e npm.

O aplicativo não instala nem configura o Tailscale. Não exige IP público, abertura de portas no roteador, VPS, STUN ou TURN. As ACLs da tailnet e o firewall local precisam permitir TCP 47621 até o host e UDP entre os peers.

## Downloads da versão 0.2.0

Baixe os executáveis completos na [release v0.2.0](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/tag/v0.2.0):

- [Windows portátil (.exe)](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/download/v0.2.0/GoLive-P2P-Portable-0.2.0.exe)
- [Linux AppImage](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/download/v0.2.0/GoLive-P2P-0.2.0.AppImage)
- [Checksums SHA-256](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/download/v0.2.0/SHA256SUMS-0.2.0.txt)

No Linux, execute `chmod +x GoLive-P2P-0.2.0.AppImage` antes de abrir.

## Desenvolvimento e builds

```bash
npm install
npm run dev
npm run lint
npm run typecheck
npm test
npm run build          # .exe portátil Windows
npm run build:linux    # AppImage Linux
npm run build:all      # ambos os pacotes
```

Os artefatos ficam em `release/`. Compile preferencialmente cada pacote em sua plataforma e valide a captura em computadores reais. O `.exe` portátil não precisa de instalação; o AppImage pode precisar de `chmod +x`.

## Como usar

1. Em todos os computadores, confirme a conexão com `tailscale status` e `tailscale ip -4`. No PowerShell, se `tailscale` não estiver no PATH, execute `& "$env:ProgramFiles\Tailscale\tailscale.exe" status`.
2. Uma pessoa informa o nome e clica **Criar sala**. O IP Tailscale do host aparece na sala.
3. As demais podem clicar **Procurar salas na tailnet**. A descoberta consulta os dispositivos online que o Tailscale já conhece e procura hosts GoLive na porta 47621. Se a sala não aparecer, informe manualmente o IP do host.
4. Todos aparecem na lista. Uma pessoa escolhe a qualidade, seleciona monitor ou janela e inicia a transmissão.
5. O transmissor pode alterar a qualidade durante o compartilhamento e acompanhar bitrate, codec, FPS, resolução, RTT, perda e estado de cada espectador.
6. Ao parar ou sair, os streams e as conexões são limpos. Se a sinalização cair brevemente, o cliente tenta reconectar usando o mesmo ID de sessão, reservado por 30 segundos. O WebRTC tenta nova oferta ICE quando a conexão falha.

Só uma pessoa transmite por vez na sala. Isso evita saturar a banda de upload em grupos pequenos; qualquer participante pode transmitir depois que a transmissão atual parar.

## Qualidade e áudio

Presets: 720p30, 720p60, 1080p30, 1080p60 e 1440p30. São metas de captura e limites iniciais de bitrate por peer; hardware, sistema e rede podem reduzir os valores. Com quatro espectadores, o transmissor envia até quatro fluxos.

No Windows, marcar **Compartilhar áudio** solicita loopback do sistema pelo Electron. No Linux, a interface permite escolher uma entrada de áudio exposta pelo PulseAudio/PipeWire, inclusive uma fonte *monitor* quando o sistema a disponibiliza. Clique **Listar entradas de áudio** para conceder a permissão e selecionar a fonte. O vídeo continua funcionando se o áudio não estiver disponível.

## Arquitetura

- `src/main`: Electron, seleção de fonte e servidor ligado somente ao IP Tailscale.
- `src/preload`: API limitada via contextBridge, com `contextIsolation: true` e `nodeIntegration: false`.
- `src/shared`: protocolo validado com Zod.
- `src/services/tailscale`: consulta à CLI oficial e descoberta opcional de hosts conhecidos.
- `src/services/signaling`: sala única, limite de cinco sessões, IDs e tokens aleatórios, retomada temporária e roteamento das mensagens pelo socket autenticado.
- `src/services/webrtc`: uma RTCPeerConnection por espectador, ICE sem STUN/TURN e H.264 preferido quando disponível.
- `src/services/stats`: getStats por peer a cada 2,5 segundos.
- `src/renderer`: interface React, diagnóstico e vídeo direto em `HTMLVideoElement`.

O Chromium expõe candidatos ICE locais sem ofuscação mDNS para permitir a negociação do IPv4 Tailscale. Participantes da sala podem ver esses IPs locais no SDP. A mídia usa DTLS/SRTP e a tailnet usa WireGuard/Tailscale.

## Diagnóstico

- `tailscale ping <IP-do-peer>` verifica conectividade. O resultado indica `direct` quando o Tailscale conecta os dispositivos diretamente e `via DERP` quando precisa de relay. Nesse caso o vídeo continua sem passar pelo host de sinalização, mas os pacotes da tailnet podem atravessar o DERP.
- Se a sala não aparecer na descoberta, use o IP manualmente. A descoberta depende de os peers estarem visíveis no `tailscale status --json` e de a porta TCP 47621 estar acessível.
- Se a sinalização conectar mas o vídeo não chegar, verifique as ACLs para UDP entre peers e os estados WebRTC/ICE no painel Diagnóstico.
- No Windows, permita o GoLive P2P no Firewall quando solicitado. No Linux, verifique permissões de captura, xdg-desktop-portal e a presença de fontes monitor de áudio.
- Se a rede cair por mais de 30 segundos, a sessão pode expirar; entre novamente.

## Limitações verificáveis

O fluxo completo entre máquinas, áudio do sistema no Windows, seleção de fontes em diferentes ambientes Linux e executáveis empacotados precisam de validação prática nesses sistemas. O ambiente de build automatizado verifica código, protocolo e empacotamento, mas não dispõe de dois desktops conectados à mesma tailnet. A descoberta é opcional e não substitui a entrada manual.
