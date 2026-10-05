/* JSON-file database. Single process; every write goes to a temp file
   and is renamed into place, so a crash never leaves a half-written file. */
const fs = require('fs');
const path = require('path');

const empty = () => ({
  users: [],
  assets: [],
  requests: [],
  purchaseOrders: [],
  sessions: {},
  counters: { asset: 1000, request: 2000, po: 3000, user: 0 },
});

function open(dataDir) {
  fs.mkdirSync(dataDir, { recursive: true });
  const file = path.join(dataDir, 'db.json');
  let data = empty();
  if (fs.existsSync(file)) data = { ...empty(), ...JSON.parse(fs.readFileSync(file, 'utf8')) };

  return {
    data,
    save() {
      const tmp = `${file}.${process.pid}.tmp`;
      fs.writeFileSync(tmp, JSON.stringify(data, null, 2), { mode: 0o600 });
      fs.renameSync(tmp, file);
    },
    nextNumber(kind, prefix) {
      data.counters[kind] += 1;
      return `${prefix}-${data.counters[kind]}`;
    },
  };
}

module.exports = { open };
