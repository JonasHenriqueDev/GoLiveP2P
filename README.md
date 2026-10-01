# GoLive P2P

Aplicativo desktop para compartilhar tela com até cinco pessoas na mesma tailnet Tailscale. O host coordena a sala por WebSocket no próprio IP Tailscale; vídeo e áudio trafegam por uma conexão WebRTC independente para cada espectador. Não há servidor central de mídia.

## Plataformas e requisitos

- Windows 10/11: executável portátil, plataforma principal. Vídeo de monitor e janela disponível. Áudio de janela usa a API por processo presente no build 20348 ou superior (na prática, Windows 11 nos computadores de uso comum); essa captura ainda requer validação em dispositivos reais.
- Linux: AppImage. A captura de tela depende do compositor e, em Wayland, do xdg-desktop-portal. O áudio fica desativado até existir filtragem segura do Discord nessa plataforma.
- Tailscale instalado, autenticado e conectado à mesma tailnet em cada computador.
- Para desenvolvimento: Node.js 22+ e npm.

O aplicativo não instala nem configura o Tailscale. Não exige IP público, abertura de portas no roteador, VPS, STUN ou TURN. As ACLs da tailnet e o firewall local precisam permitir TCP 47621 até o host e UDP entre os peers.

## Downloads da versão 0.3.0

Baixe os executáveis completos na [release v0.3.0](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/tag/v0.3.0):

- [Windows portátil (.exe)](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/download/v0.3.0/GoLive-P2P-Portable-0.3.0.exe)
- [Linux AppImage](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/download/v0.3.0/GoLive-P2P-0.3.0.AppImage)
- [Checksums SHA-256](https://github.com/JonasHenriqueDev/GoLiveP2P/releases/download/v0.3.0/SHA256SUMS-0.3.0.txt)

No Linux, execute `chmod +x GoLive-P2P-0.3.0.AppImage` antes de abrir.

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
4. Todos aparecem na lista, com ping Tailscale ao lado do nome. Uma pessoa escolhe qualidade e teto de bitrate por espectador, seleciona um monitor inteiro ou uma janela/aplicativo e inicia a transmissão.
5. O transmissor pode alterar a qualidade e o bitrate durante o compartilhamento e acompanhar bitrate real, codec, FPS, resolução, RTT, perda e estado de cada espectador.
6. Ao parar ou sair, os streams e as conexões são limpos. Se a sinalização cair brevemente, o cliente tenta reconectar usando o mesmo ID de sessão, reservado por 30 segundos. O WebRTC tenta nova oferta ICE quando a conexão falha.

Só uma pessoa transmite por vez na sala. Isso evita saturar a banda de upload em grupos pequenos; qualquer participante pode transmitir depois que a transmissão atual parar.

## Qualidade e áudio

Presets: 720p30, 720p60, 1080p30, 1080p60 e 1440p30. São metas de captura. O usuário também pode escolher um teto de bitrate de 0,5 a 20 Mbps por espectador; o WebRTC pode transmitir abaixo dele conforme a rede. Com quatro espectadores, o transmissor envia até quatro fluxos.

Ao compartilhar **uma janela no Windows compatível**, marcar **Áudio do aplicativo (janela)** solicita a captura da árvore de processos da janela selecionada. O helper bloqueia janelas do Discord e do próprio GoLive e interrompe o áudio se um desses aplicativos aparecer como subprocesso da árvore capturada. O microfone não é capturado como entrada. O loopback geral do Electron foi removido para evitar misturar origens de áudio.

Ao compartilhar **um monitor**, a transmissão é apenas de vídeo. A mistura de áudio dos aplicativos permitidos, com exclusão do Discord, do microfone, do próprio app e de sessões sem origem confiável, ainda não foi implementada nem validada. É bloqueada para evitar vazamentos. No Windows 10 comum (ex.: build 19045) e no Linux, o áudio de janela também fica desativado porque a API por processo usada pelo helper não está disponível. Uma aba do Discord aberta em navegador não pode ser separada das demais abas pelo processo; portanto, não selecione o navegador como fonte de áudio se ele estiver reproduzindo Discord. A exclusão do Discord **não está garantida** até haver teste em máquinas reais.

A [documentação da Microsoft sobre `AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS`](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_process_loopback_params) especifica como requisito mínimo o build 20348. Enumerar sessões de áudio no Windows 10 comum identifica parte das origens, mas não fornece uma API nativa equivalente para capturar separadamente cada sessão sem driver ou componente adicional. Por isso o app não usa loopback agregado como substituto.

## Ping e reports de logs

O ping ao lado de cada participante é medido pela CLI `tailscale ping` e atualizado periodicamente. `— ms` indica que não foi possível obter resposta. A rota direta ou DERP aparece ao passar o cursor sobre o ping.

**Baixar meu log TXT** abre uma caixa para escolher onde salvar o report local. **Enviar log** permite escolher outro participante. O servidor apenas encaminha o texto; o destinatário vê quem enviou e escolhe **Baixar TXT** ou **Ignorar**. O recebimento não salva arquivos automaticamente. O report contém eventos de informação, avisos e erros da aplicação e pode incluir caminhos locais ou endereços Tailscale; confira antes de enviá-lo.

## Arquitetura

- `src/main`: Electron, seleção de fonte e servidor ligado somente ao IP Tailscale.
- `src/preload`: API limitada via contextBridge, com `contextIsolation: true` e `nodeIntegration: false`.
- `src/shared`: protocolo validado com Zod.
- `src/services/tailscale`: consulta à CLI oficial e descoberta opcional de hosts conhecidos.
- `src/services/signaling`: sala única, limite de cinco sessões, IDs e tokens aleatórios, retomada temporária e roteamento das mensagens pelo socket autenticado.
- `src/services/webrtc`: uma RTCPeerConnection por espectador, ICE sem STUN/TURN e H.264 preferido quando disponível.
- `src/services/stats`: getStats por peer a cada 2,5 segundos.
- `native/windows`: helper C++ baseado na API oficial de áudio por processo do Windows. PCM é entregue diretamente a um AudioWorklet, que cria a track enviada pelo WebRTC.
- `src/main/logs.ts`: log persistente rotativo e exportação por diálogo nativo.
- `src/renderer`: interface React, diagnóstico e vídeo direto em `HTMLVideoElement`.

O Chromium expõe candidatos ICE locais sem ofuscação mDNS para permitir a negociação do IPv4 Tailscale. Participantes da sala podem ver esses IPs locais no SDP. A mídia usa DTLS/SRTP e a tailnet usa WireGuard/Tailscale.

## Diagnóstico

- `tailscale ping <IP-do-peer>` verifica conectividade. O resultado indica `direct` quando o Tailscale conecta os dispositivos diretamente e `via DERP` quando precisa de relay. Nesse caso o vídeo continua sem passar pelo host de sinalização, mas os pacotes da tailnet podem atravessar o DERP.
- Se a sala não aparecer na descoberta, use o IP manualmente. A descoberta depende de os peers estarem visíveis no `tailscale status --json` e de a porta TCP 47621 estar acessível.
- Se a sinalização conectar mas o vídeo não chegar, verifique as ACLs para UDP entre peers e os estados WebRTC/ICE no painel Diagnóstico.
- No Windows, permita o GoLive P2P no Firewall quando solicitado. No Linux, verifique permissões de captura e xdg-desktop-portal.
- Se a rede cair por mais de 30 segundos, a sessão pode expirar; entre novamente.

## Limitações verificáveis

O fluxo completo entre máquinas, áudio por processo no Windows, seleção de fontes em diferentes ambientes Linux e executáveis empacotados precisam de validação prática nesses sistemas. O ambiente de build automatizado verifica código, protocolo e empacotamento, mas não dispõe de dois desktops conectados à mesma tailnet nem de dispositivos reais de áudio do Windows. A descoberta é opcional e não substitui a entrada manual. Para concluir a funcionalidade de áudio solicitada, é necessário implementar um misturador por processo para monitores e validar a atribuição de sessões em Windows 10 e 11.
