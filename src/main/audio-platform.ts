/** Process loopback has been reported to work on updated Windows 10 2004+.
 * The native helper probes the API at capture time and fails closed.
 */
export function canTryProcessLoopback(platform: string, release: string): boolean {
  if (platform !== 'win32') return false;
  const build = Number(release.split('.')[2]);
  return Number.isInteger(build) && build >= 19041;
}
