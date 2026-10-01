const reasons: Record<string, string> = {
  INVALID_WINDOW: 'A janela selecionada não existe mais ou o identificador não é uma janela do Windows.',
  TARGET_NOT_FOUND: 'Não foi possível identificar o processo da janela.',
  TARGET_EXITED: 'O processo da janela foi encerrado.',
  DISCORD_WINDOW_BLOCKED: 'O áudio dessa janela pertence ao Discord e foi bloqueado.',
  DISCORD_DESCENDANT_BLOCKED: 'O Discord faz parte dos subprocessos da janela; áudio bloqueado.',
  DISCORD_CHANGED: 'O Discord apareceu na árvore de processos; áudio interrompido.',
  GOLIVE_WINDOW_BLOCKED: 'O áudio do próprio GoLive foi bloqueado.',
  GOLIVE_CHANGED: 'O GoLive apareceu na árvore de processos; áudio interrompido.',
  AUDIO_ACTIVATION: 'O Windows recusou a API de áudio por processo nesta instalação.',
  AUDIO_FORMAT: 'O dispositivo de áudio recusou o formato de captura.',
  AUDIO_START: 'O dispositivo de áudio não iniciou a captura.'
};

export function explainAudioFailure(raw: string): string {
  const detail = raw.trim();
  const code = detail.split(/\s+/)[0];
  return (reasons[code] || 'A captura de áudio falhou.') + ' [' + detail.slice(0, 160) + ']';
}
