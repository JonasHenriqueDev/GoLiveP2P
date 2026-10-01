import type { TailscaleStatus } from '../services/tailscale/status';
import type { UpdateState } from '../main/updater';
import type {
  NativeMethod,
  NativeData,
  NativeResults,
  NativeEvent,
} from '../shared/native-media';
declare global {
  interface Window {
    golive: {
      platform: string;
      mediaRequest: <M extends NativeMethod>(
        method: M,
        data: NativeData<M>,
      ) => Promise<NativeResults[M]>;
      onMediaEvent: (callback: (event: NativeEvent) => void) => () => void;
      systemInfo: () => Promise<{
        name: string;
        release: string;
        arch: string;
      }>;
      updateState: () => Promise<UpdateState>;
      installUpdate: () => Promise<boolean>;
      setSessionActive: (active: boolean) => void;
      onUpdateState: (callback: (state: UpdateState) => void) => () => void;
      tailscaleStatus: () => Promise<TailscaleStatus>;
      discoverHosts: () => Promise<
        { ip: string; name: string; participants: number; full: boolean }[]
      >;
      pingPeer: (
        ip: string,
      ) => Promise<{ ms: number | null; route: 'direct' | 'DERP' | 'unknown' }>;
      audioSupport: () => Promise<{ available: boolean; message: string }>;
      startAudioCapture: () => Promise<void>;
      stopAudioCapture: () => Promise<void>;
      onAudioChunk: (callback: (chunk: Uint8Array) => void) => () => void;
      onAudioError: (callback: (message: string) => void) => () => void;
      getLogReport: () => Promise<string>;
      saveLogReport: (text: string, name?: string) => Promise<boolean>;
      log: (
        level: 'info' | 'warn' | 'error',
        scope: string,
        message: string,
      ) => void;
      createRoom: () => Promise<{ ip: string; port: number }>;
      closeRoom: () => Promise<void>;
      listSources: () => Promise<
        {
          id: string;
          name: string;
          thumbnail: string;
          kind: 'screen' | 'window';
        }[]
      >;
      selectSource: (id: string) => Promise<void>;
    };
  }
}
export {};
