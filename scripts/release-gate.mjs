import { readFileSync } from 'node:fs';
const validation = JSON.parse(
  readFileSync(
    new URL('../docs/native-validation.json', import.meta.url),
    'utf8',
  ),
);
const packageJson = JSON.parse(
  readFileSync(new URL('../package.json', import.meta.url), 'utf8'),
);
const requirements = [
  'releaseApproved',
  'windows10',
  'windows11',
  'crossDevice',
  'audioExclusions',
  'latencyAndLoad',
  'installerInspected',
  'licensesAudited',
  'congestionControl',
];
const missing = requirements.filter((key) => validation[key] !== true);
if (validation.version !== packageJson.version)
  missing.push('matching version');
if (missing.length)
  throw new Error('Native migration release blocked: ' + missing.join(', '));
console.log('Native release evidence gates passed.');
