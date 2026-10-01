import { expect, it } from 'vitest';
import { explainAudioFailure } from './audio-errors';

it('preserves native error details and explains a Windows 10 activation failure', () => {
  expect(explainAudioFailure('AUDIO_ACTIVATION 0x80004005')).toContain('0x80004005');
  expect(explainAudioFailure('AUDIO_ACTIVATION 0x80004005')).toContain('recusou a API');
});
