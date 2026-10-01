# GoLive P2P

Aplicativo desktop Electron para compartilhar tela com até cinco participantes. Um computador hospeda a sinalização WebSocket no próprio IPv4 Tailscale; o vídeo e o áudio usam conexões WebRTC independentes entre transmissor e cada espectador. O host não retransmite mídia.

## Requisitos

- Windows 10/11 (plataforma principal). Linux permanece disponível como plataforma secundária.
- Node.js 22 ou superior e npm
- Tailscale instalado, autenticado e conectado à mesma tailnet em todos os computadores
- Permissão de captura de tela do sistema operacional

## Instalar e executar

```bash
npm install
npm run dev
```

Para checar o código:

```bash
npm run lint
npm run typecheck
npm test
npm run build
```

`npm run build` gera o executável portátil `.exe` para Windows em `release/`. Para gerar um AppImage Linux, use `npm run build:linux`. O executável não exige instalação. Prefira validar o arquivo em Windows.

## Uso em dois computadores

1. Em A e B, abra o PowerShell e execute `tailscale status` e `tailscale ip -4`. Ambos devem estar conectados. Se o comando não estiver no PATH, use `& "$env:ProgramFiles\Tailscale\tailscale.exe" status`.
2. Em A, digite um nome e clique **Criar sala**. Copie o IPv4 Tailscale exibido.
3. Em B, digite um nome e o IP de A e clique **Entrar na sala**.
4. Em A, escolha 1080p60 ou 720p30 e clique **Compartilhar tela**. Escolha o monitor ou janela.
5. B recebe o vídeo. A pode parar a transmissão. Até três outros espectadores podem entrar.
6. No terminal, `tailscale ping <peer>` verifica a conectividade. O resultado indica `direct` quando há conexão direta entre os dispositivos e `via DERP` quando o Tailscale precisou usar relay. O aplicativo continua P2P no nível WebRTC; o relay Tailscale pode encaminhar pacotes quando o caminho direto não está disponível.

## Arquitetura

- `src/main`: ciclo de vida Electron, detecção de fontes e host WebSocket na interface Tailscale.
- `src/preload`: API limitada via contextBridge; renderer sem Node.js.
- `src/shared`: protocolo tipado e validação Zod.
- `src/services/tailscale`: CLI oficial `tailscale status --json` e `tailscale ip -4`.
- `src/services/signaling`: sala única de até cinco usuários; IDs de sessão aleatórios; roteamento autenticado pelo socket.
- `src/services/webrtc`: uma RTCPeerConnection por espectador; ICE sem servidores STUN/TURN; H.264 preferido quando disponível; bitrate máximo por preset.
- `src/services/stats`: amostragem de getStats a cada 2,5 segundos.
- `src/renderer`: interface React com seleção de fontes, sala, vídeo e diagnóstico.

O Chromium expõe candidatos ICE locais sem ofuscação mDNS para permitir que o IPv4 Tailscale seja negociado entre máquinas. Isso também torna IPs locais visíveis aos participantes da sala. O servidor de sinalização escuta só no IP Tailscale. A sessão WebRTC usa criptografia DTLS/SRTP, além do túnel WireGuard do Tailscale.

## Diagnóstico e problemas comuns

- **Tailscale desconectado:** execute `tailscale status` e conecte o serviço; criação e entrada ficam desabilitadas.
- **Não conecta ao host:** teste `tailscale ping <IP-do-host>`; confira se as ACLs da tailnet permitem TCP 47621 e UDP entre peers.
- **Sinalização conecta mas vídeo não chega:** confira as ACLs para tráfego UDP entre os IPs Tailscale; veja o estado WebRTC/ICE na seção Diagnóstico.
- **Firewall do Windows:** permita o GoLive P2P nas redes apropriadas quando o Windows solicitar. O host precisa aceitar TCP 47621 em seu endereço Tailscale e os peers precisam permitir o tráfego UDP do WebRTC. Confira também as ACLs da tailnet.
- **Captura indisponível no Linux:** em Wayland, verifique xdg-desktop-portal e permissão de captura de tela. A seleção de janela pode variar conforme o compositor.
- **Áudio não chega:** a captura de áudio do sistema depende da plataforma. A primeira versão habilita loopback do Electron no Windows; Linux avisa quando não há track de áudio.
- **Qualidade abaixo do preset:** resolução, FPS e bitrate são metas; captura, hardware e rede podem reduzi-los. Cada espectador exige upload separado, até quatro vezes o bitrate individual.

## Limitações conhecidas

A reconexão automática de WebRTC usa ICE restart quando a conexão falha. Se a sinalização cair, o usuário precisa entrar novamente na sala. A sala tem um único transmissor simultâneo. A interface não descobre hosts automaticamente; o IP Tailscale é informado manualmente. A validação real entre máquinas e do executável portátil Windows depende de computadores Windows com Tailscale e desktop disponíveis.

## Melhorias futuras

Descoberta opcional do host na tailnet, reconexão da sala com identidade de sessão, presets adicionais, telemetria detalhada por espectador e validação de captura de áudio em mais distribuições Linux.
