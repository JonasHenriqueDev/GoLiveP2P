# Continuidade do áudio — 0.6.0-qt.3 (experimental)

O usuário relatou áudio picotando no Fedora, usando o cliente antigo porque o cliente Qt entrava na sala sem mostrar vídeo. A instalação Windows 0.6.0-qt.2, sozinha, não atualiza o jitter buffer do receptor Linux.

## Correções

- A enumeração de sessões e a ativação/reinicialização WASAPI foram retiradas do ciclo de envio de 10 ms. Um gestor separado mantém as árvores permitidas; falha na atribuição continua bloqueando somente o áudio.
- A checagem de segurança continua enumerando todos os relacionamentos entre processos, mas consulta identidades caras somente da árvore capturada, seus ancestrais e os ancestrais do motor. Processos desconhecidos dentro dessa árvore continuam bloqueados. A janela é revalidada durante a mistura.
- FIFO estéreo com reserva de captura de 80 ms, limite de 300 ms e ajuste gradual de diferenças entre relógios. Buffers são limpos quando a captura perde a identidade/autorização. Não há entrada de microfone nem captura agregada.
- Pedido de capacidade WASAPI de 200 ms; não equivale a esperar 200 ms antes de ler. O tamanho informado pela API process-loopback nesta máquina foi incoerente em uma execução; ele é registrado apenas quando plausível e não controla endereçamento de memória.
- Prazos absolutos e timer de alta resolução em vez de contar despertares periódicos; recuperação limitada de despertares coalescidos e estatísticas de atrasos. MMCSS opcional para captura/envio. A telemetria periódica não escreve no IPC pela thread de áudio.
- Opus de 20 ms, recuperação PLC e FEC habilitadas, com eventos de perda encaminhados ao decoder. FEC depende do modo escolhido internamente pelo Opus; não é garantia de recuperar toda perda. O receptor usa jitter RTP de 120 ms e buffer de reprodução. No Linux prefere PulseAudio/PipeWire-Pulse, com fallback automático.
- Sinalização válida recebida durante a inicialização do processo de mídia fica numa fila limitada; parar/remover/encerrar cancela esses pedidos. Antes, a oferta podia ser descartada como “Motor indisponível”. O Linux usa decoder H.264 em CPU para evitar seleção automática de GPU indisponível.
- Diagnósticos incluem nível da FIFO, falta de amostras, descartes, tempo da checagem de origem, atraso do scheduler e recuperação Opus/RTP.

## Executado nesta máquina

Windows 10 Pro 22H2, build 19045.6466, RTX 2060. Resultados de desenvolvimento; não representam validação Windows 11 ou audição no Fedora.

- Teste de rajadas na FIFO: zero pacotes de silêncio após a pré-carga.
- Diferença de relógios simulada por cinco minutos: zero underflows e zero descartes, reserva limitada.
- Teste de autorização: limpar a origem elimina todas as amostras pendentes.
- Pacer: despertares irregulares não perdem intervalos de amostras; atraso grande tem recuperação limitada.
- Opus/RTP real com perda intencional de seis pacotes: 96.000 frames decodificados, zero frames silenciosos, 5.760 amostras reconstruídas por PLC. Não comprova FEC universal ou ausência de artefatos em música/jogos.
- Teste de IPC com executável de mídia que demora para iniciar: oferta anterior ao evento `ready` permanece e é atendida após iniciar.
- Integração anterior à otimização final recebeu vídeo, áudio e tela cheia, mas a telemetria mostrou underflows de captura. Esse resultado motivou mais correções; RMS positivo não foi aceito como prova de continuidade.

## Integração final Windows

- Execução de 90 segundos com a janela de teste Win32 coberta e redimensionada, 1080p60/NVENC, duas instâncias Qt e transmissão P2P pelo IP Tailscale desta máquina: **passou**.
- 1.321 frames de prévia, 2.062 frames apresentados no receptor e 3.218.880 frames de áudio decodificados. Esses contadores são de apresentação/áudio, não uma medição de FPS de rede ou latência física.
- Zero underflows de captura, zero amostras descartadas pela FIFO, zero intervalos de envio perdidos. Receptor: zero perdas/atrasos registrados na execução, zero gaps no Opus e zero buffers descartados pelo sink WASAPI. O teste executou enquanto o instalador era comprimido em segundo plano; não é benchmark geral de carga.
- Windows: testes Qt (12 verificações), player, continuidade, políticas (9) e WASAPI (18), jitter RTP, Opus e análise MSVC executados. O pipeline recalcula a latência quando o GStreamer solicita, em vez de ignorar a mensagem.
- A negociação sem áudio encontrou um timeout: o primer tinha apenas 10 ms para um encoder agora configurado em 20 ms. Ele foi ajustado para um frame completo de silêncio, sem capturar áudio de origem não autorizada. A repetição passou: vídeo recebido, zero frames de áudio e RMS zero.
- Linux Ubuntu 24.04 / GStreamer 1.24.2 no CI: cinco grupos CTest, incluindo o mesmo teste de perda Opus (6 pacotes perdidos, 96.000 frames recuperados, nenhum frame silencioso), e inicialização do pacote passaram. A instalação/remoção RPM foi testada em Fedora 44, sem equivaler à recepção ou audição no computador do usuário.

## Pendente

Confirmar vídeo e áudio no Fedora do usuário. A causa do vídeo ausente no Fedora não foi comprovada por log desse dispositivo; a corrida de inicialização foi reproduzida em teste de IPC e corrigida, e o decoder explícito é uma medida de compatibilidade. Não há promessa de áudio “perfeito” em redes/dispositivos arbitrários.

Referências: [IAudioClient::Initialize](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize), [captura por processo](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/), [Opus encoder](https://gstreamer.freedesktop.org/documentation/opus/opusenc.html), [Opus decoder](https://gstreamer.freedesktop.org/documentation/opus/opusdec.html).

## Conferência dos pacotes finais

- Instalador Windows 0.6.0-qt.3 extraído e instalado nesta máquina: 868 arquivos conferidos contra o manifesto. Integração pelo executável instalado passou: 542 frames de prévia, 491 apresentados, 698.880 frames de áudio, zero underflows, descartes de FIFO e intervalos de envio perdidos.
- CI 37028031433, código 35c1bb97: build Linux e RPM passaram. RPM instalado, verificado com rpm -V, iniciado com motor pronto em Fedora 44/Xvfb e removido. Isso verifica empacotamento/inicialização, não transmissão real nesse Fedora.
- SHA-256 dos três artefatos finais conferidos. O rpmlint continua reportando erros/avisos nas bibliotecas privadas e metadados; o pacote experimental não atende integralmente às regras de um repositório Fedora oficial.
