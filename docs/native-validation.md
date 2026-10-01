# Validação nativa nesta máquina

Data: 2026-10-01. Base inicial: `3dfae85`. Build em desenvolvimento: `0.5.0-native.1`.

## Ambiente observado

- Windows 10 Pro 22H2, **10.0.19045.6466**, x64.
- NVIDIA GeForce RTX 2060, driver `32.0.16.1088`; também há adaptador virtual Parsec.
- Dois monitores enumerados como `DISPLAY1` e `DISPLAY2`.
- GStreamer 1.26.7 MSVC x64, runtime/SDK verificados pelos SHA-256 oficiais fixados no script.
- MSVC 14.50.35717 e Windows SDK 10.0.26100.0.
- Tailscale ativo; IP local 100.75.12.74. Fedora 100.94.54.119 está disponível segundo o usuário e tinha GoLive 0.3. Não havia serviço de sinalização na porta 47621 durante a sondagem inicial.

## Executado

Os arquivos brutos ficam em `work/native-results` e `work/native-probe` (não publicados, pois incluem nomes de janelas, PIDs e IPs). `native/windows/probe.mjs` cria dois processos nativos e encaminha somente SDP/ICE; os pacotes de mídia passam diretamente entre eles.

| Cenário | Evidência obtida | Limite |
| --- | --- | --- |
| Monitor principal, DXGI, NVENC, 1920×1080/60, teto 6 Mbps | 797 frames codificados em 13,286 s (~60 FPS); receptor decodificou JPEGs; ICE/DTLS conectados | Dois processos no mesmo PC; sem validação entre computadores |
| Segundo monitor, DXGI, NVENC, 1920×1080/60 | 738 frames em 12,294 s (~60 FPS), vídeo recebido | Não é teste de mudança de monitor durante uma sessão |
| Janela WGC coberta por outra janela de teste | Frames permanecem verdes (alvo), sem a janela vermelha que a cobre | Janela Win32 de teste; não generaliza para jogos/DRM/janelas minimizadas |
| Redimensionamento da janela WGC | Frames continuam após mudar a janela de 900×640 para 650×460 | Saída mantém o preset; revisão de proporção da apresentação foi necessária |
| Áudio da janela de teste, 440 Hz gerados por waveOut no mesmo processo | API process-loopback iniciou no build 19045.6466; receptor recebeu 723.840 frames de áudio e RMS não nulo | Não prova isolamento em qualquer aplicativo |
| Monitor com lista explícita permitindo somente o executável de teste | Uma origem nativa admitida; áudio recebido (660.000 frames no ponto de medição) | Mistura com múltiplos aplicativos e exclusão simultânea do Discord ainda não validadas |
| Bitrate alterado de 6 para 2 Mbps | Comando aplicado ao encoder durante o fluxo | Falta medir adaptação e congestionamento por receptor |
| Remover/recriar conexão | Novas conexões chegaram a connected e o receptor voltou a entregar frames | Foram encontrados candidatos ICE precoces; fila nativa foi adicionada e requer repetição final |
| OpenH264 por CPU | Fluxo nativo conectou e entregou frames após correção do keyframe inicial | Um erro posterior de estatísticas do receptor foi corrigido; benchmark final pendente |
| Interface Electron real | Criou sala pelo IP Tailscale e exibiu prévia nativa com largura 960 | Primeiro script encontrou falha de import ESM do updater, corrigida; limpeza do script também corrigida |
| Verificações TypeScript | Lint, typecheck e 29 testes passaram | Repetir depois das últimas correções e antes de empacotar |
| Auditoria npm | Zero vulnerabilidades após Electron 44.5.1 e Vitest 5.0.3 | Não equivale à auditoria das bibliotecas nativas |

Uma amostra de `nvidia-smi` durante o teste mostrou GPU 49%, encoder 9%, decoder 13%, 1.694 MiB. Havia outros aplicativos ativos, incluindo reprodução de vídeo; esses valores **não são um benchmark atribuído ao GoLive**. A medição isolada de CPU/GPU e latência fim a fim permanece pendente.

## Ainda não validado / bloqueios

- Windows 11 real; máquina não disponível nesta sessão até este registro.
- Remoção da borda WGC em Windows 10. O atributo solicitado não comprova remoção; não há solução sem borda garantida para janela coberta.
- Dois computadores reais e depois cinco participantes reais. O Fedora informado pelo usuário será usado para teste do cliente; resultado ainda pendente.
- Áudio simultâneo de Discord e aplicativo permitido, árvores dinâmicas, processos órfãos/renomeados, falhas de identificação e garantia de exclusão de microfone reproduzido por outro aplicativo.
- Controle de congestionamento independente por receptor.
- Prévia local é 15 FPS; apresentação Windows remota é 30 FPS em JPEG. O encode/fluxo testado é 60 FPS. Renderização direta de GPU ainda não foi implementada.
- Latência, carga sustentada, reconexão de rede/Tailscale e sinalização entre dispositivos.
- Inspeção final do instalador, execução do pacote e checksum; atualização efetiva entre duas versões instaladas.
- Auditoria de todas as licenças/DLLs/plugins antes de publicar o pacote Windows.

`native-validation.json` conserva os gates como false. Nenhuma release deve anunciar a migração Windows como concluída com essas pendências. Uma pré-release **somente cliente Linux** poderá ser publicada, a pedido do usuário, para viabilizar os testes no Fedora, com limitações explícitas.
