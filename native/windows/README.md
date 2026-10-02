# Motor Windows nativo

O motor atual é media-engine.cpp com captura PrintWindow/DXGI/WGC, H.264/Opus, WebRTC e áudio por processo. Electron mantém a interface e sinalização. Consulte ../../docs/native-media-windows.md e ../../docs/native-validation.md para evidências e limites.

Build: npm run native:setup (SDK verificado) e npm run native:build (MSVC/Windows SDK). O runtime empacotado inclui window-capture.exe, DLLs, plugins, licenças e manifesto de hashes. Não é preciso instalar SDK na máquina do usuário.

Testes de áudio: pwsh -File native/windows/process-audio-test.ps1; node native/windows/policy-test.mjs; node scripts/native-chromium-audio-test.mjs. Use --chrome-source e --unrelated-chrome para conferir dois aplicativos emitindo tons distintos; --blocked-fixture testa uma fixture renomeada Discord.exe e exige silêncio com vídeo funcionando. A simulação não equivale ao Discord real.

Audio-capture.cpp é o auxiliar antigo, preservado para compatibilidade. Ele não é o motor de mídia atual. Nenhum caminho do motor usa loopback agregado do monitor ou captura de microfone como dispositivo.
