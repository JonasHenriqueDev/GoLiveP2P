# Migração de interface Qt — 2026-10-02

Solicitação atual do usuário substitui a decisão anterior de manter Electron. Interface implementada em Qt Widgets 6.8.3 + C++; sala, descoberta, ping, logs, reconexão e updater portados. Não há Electron/React/TypeScript no pacote Qt. O protocolo de sala é compatível com os clientes anteriores; essa compatibilidade ainda requer confirmação entre dispositivos após a migração da interface.

Máquina: Windows 10 Pro 22H2 10.0.19045.6466, RTX 2060, dois monitores, MSVC 19.50.35725.0. Qt oficial MSVC x64 via aqtinstall, Core/Gui/Widgets/Network/WebSockets ligados dinamicamente. SDK completo necessário apenas para desenvolver.

## Executado

- Testes C++ Qt: IP da tailnet, limites de captura/método/bitrate, validação de sinalização e respostas IPC, sala real com WebSocket, descoberta HTTP na mesma porta, máximo cinco participantes e reconexão preservando ID/token. Testes locais Windows passaram. Cinco clientes locais não equivalem a cinco máquinas.
- Teste entre dois aplicativos **Qt completos**: janela Win32 de teste coberta/redimensionada, tom waveOut de 440 Hz no processo da janela, captura PrintWindow e NVENC 720p30. Host apresentou 535 quadros de prévia; receptor apresentou 412 quadros e recebeu 632.640 frames de áudio, RMS máximo ~0,003249. Imagem recebida inspecionada: conteúdo verde da janela alvo. Relatórios locais em `work/qt-smoke/2026-10-02T04-33-51-656Z`. Porta de integração 47622 para não interferir em possíveis salas do usuário; porta padrão continua 47621.
- Tentativa de renderização D3D11 direta usando HWND Qt de outro processo: nenhuma entrega de vídeo no prazo de cinco segundos; tentativa retirada do motor e da interface de produção. Não registrada como renderização GPU aprovada. Apresentação JPEG continua funcional; GPU/NVENC indica codificação, não renderização direta.
- O build Linux compilou UI e receptor C++ no CI. Primeira tentativa falhou por ICU ausente no SDK; corrigido o download de ICU. Segunda tentativa detectou ICU ausente no pacote; corrigida a busca/cópia de dependências. Build final ainda precisa de confirmação.

## Pendente

Instalador Qt final, inspeção/checksums e execução do payload; teste final após todas as correções; prévias DWM e duplo clique na versão Qt; execução do novo cliente Linux no Fedora; Minecraft em gameplay com áudio; áudio exclusivo com Discord real tocando simultaneamente; Windows 11; atualização entre duas versões Qt; carga/latência sustentada; congestionamento independente por receptor e auditoria completa das licenças/plugins. Resultados da interface Electron em native-validation.md são históricos e não validam automaticamente a interface Qt.
