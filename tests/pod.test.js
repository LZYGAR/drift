// Runs src/pod.js (the script that lives inside the Apple Music page) against a fake MusicKit, so the
// commands that matter most can be checked without a signed-in session. Run by bin/test when node exists.

const assert = require("node:assert");
const path = require("node:path");

const realSetInterval = global.setInterval;
const realSetTimeout = global.setTimeout;
function setup(overrides = {}) {
  global.window = { pod: null, qt: null, ...overrides.window };
  global.document = { querySelectorAll: () => [], title: "", querySelector: () => null };
  global.navigator = {};
  global.setTimeout = () => 0; // the script's polling timers are not wanted here
  global.setInterval = () => 0;
  const published = [];
  window.pod = { ingest: (s) => published.push(JSON.parse(s)) };
  const ops = [];
  const music = {
    storefrontId: "us",
    isAuthorized: true,
    autoplayEnabled: true,
    player: {},
    queue: { items: [{ id: "a" }, { id: "cur" }], position: 1 },
    nowPlayingItem: { id: "cur", title: "Current", artistName: "Cur Artist", attributes: { name: "Current", artistName: "Cur Artist" } },
    api: { music: () => Promise.reject(new Error("no route")) },
    playNext: (d) => { ops.push(["playNext", d]); return Promise.resolve(); },
    setQueue: (d) => { ops.push(["setQueue", d]); return Promise.resolve(); },
    play: () => Promise.resolve(),
    addEventListener() {},
    ...overrides.music,
  };
  window.MusicKit = { getInstance: () => music };
  delete require.cache[require.resolve(path.join(__dirname, "../src/pod.js"))];
  require(path.join(__dirname, "../src/pod.js"));
  return { published, ops, music, exec: window.__podExec };
}

const settle = async (n = 20) => { for (let i = 0; i < n; i++) await new Promise((r) => setImmediate(r)); };
const tests = [];
const test = (name, fn) => tests.push([name, fn]);

test("a snapshot says how many songs are queued after the current one", async () => {
  const { published, exec } = setup();
  exec("noop", "", "", 0);
  await settle();
  const state = published.filter((p) => p.type === "state").pop();
  assert.strictEqual(state.upcoming, 0);
  assert.strictEqual(state.queueLength, 2);
  assert.strictEqual(state.queuePosition, 1);
});

test("Apple's autoplay is switched off while similar songs is on, and put back after", async () => {
  const { published, music, exec } = setup({ window: { __podNoAutoplay: 1 } });
  exec("noop", "", "", 0);
  await settle();
  assert.strictEqual(music.autoplayEnabled, false);
  assert.strictEqual(published.filter((p) => p.type === "state").pop().autoplay, 0);
  window.__podNoAutoplay = 0;
  exec("noop", "", "", 0);
  await settle();
  assert.strictEqual(music.autoplayEnabled, true);
});

test("similar: tries the candidates in order, skips played songs, and queues right after the current one", async () => {
  const { published, ops, exec } = setup({
    music: {
      api: {
        music: (p, params) => {
          const term = (params && params.term) || "";
          if (/Missing/.test(term)) return Promise.resolve({ results: { songs: { data: [{ id: "x1", type: "songs", attributes: { name: "Missing", artistName: "Totally Different" } }] } } });
          if (/Second Wind/.test(term)) return Promise.resolve({ results: { songs: { data: [
            { id: "cur", type: "songs", attributes: { name: "Already Played", artistName: "Third Band" } },
            { id: "s2", type: "songs", attributes: { name: "Second Wind", artistName: "Third Band" } }] } } });
          return Promise.resolve({ results: { songs: { data: [] } } });
        },
      },
    },
  });
  exec("noop", "", "", 0); // records the song that is playing as heard
  await settle();
  exec("similar", JSON.stringify({ songs: [{ title: "Missing", artist: "Some Band" }, { title: "Second Wind", artist: "Third Band" }] }), "", 0);
  await settle();
  const done = published.filter((p) => p.type === "similar");
  assert.deepStrictEqual(done.map((d) => [d.state, d.title, d.id]), [["added", "Second Wind", "s2"]]);
  assert.deepStrictEqual(ops, [["playNext", { song: "s2" }]]);
});

test("similar: says so when none of the candidates are on Apple Music", async () => {
  const { published, ops, exec } = setup({ music: { api: { music: () => Promise.resolve({ results: { songs: { data: [] } } }) } } });
  exec("similar", JSON.stringify({ songs: [{ title: "Nope", artist: "Nobody" }] }), "", 0);
  await settle();
  const result = published.filter((p) => p.type === "similar");
  assert.strictEqual(result.length, 1);
  assert.strictEqual(result[0].state, "error");
  assert.deepStrictEqual(ops, []);
});

test("search lists artists first, then songs", async () => {
  const { published, exec } = setup({
    music: { api: { music: () => Promise.resolve({ results: {
      artists: { data: [{ id: "10", type: "artists", attributes: { name: "The Midnight" } }] },
      songs: { data: [{ id: "i.1", type: "songs", attributes: { name: "Night Drive", artistName: "The Midnight" } }] } } }) } },
  });
  exec("search", "midnight", "", 7);
  await settle();
  const rows = published.find((p) => p.type === "rows" && p.token === 7).rows;
  assert.deepStrictEqual(rows.map((r) => [r.type, r.label]), [["artists", "The Midnight"], ["songs", "Night Drive"]]);
  assert.strictEqual(rows[0].detail, "Artist");
});

test("an artist's page: top songs first, then every album, newest first, over several pages", async () => {
  const album = (id, name, date, n) => ({ id, type: "albums", attributes: { name, releaseDate: date, trackCount: n } });
  const { published, exec } = setup({
    music: { api: { music: (p) => {
      if (/offset=25/.test(p)) return Promise.resolve({ data: { data: [album("al.3", "Middle", "2017-02-02", 7)] } });
      if (/\/artists\/10\/albums$/.test(p)) return Promise.resolve({ data: { data: [album("al.1", "Old", "2014-01-01", 10), album("al.2", "New", "2020-05-05", 1)], next: "/v1/catalog/us/artists/10/albums?offset=25" } });
      return Promise.reject(new Error("unexpected " + p));
    } } },
  });
  exec("artist", "10", "", 2);
  await settle();
  const rows = published.find((p) => p.token === 2).rows;
  assert.deepStrictEqual(rows.map((r) => r.label), ["Top Songs", "New", "Middle", "Old"]);
  assert.strictEqual(rows[1].detail, "2020 · 1 song");
  assert.strictEqual(rows[2].detail, "2017 · 7 songs");
  assert.ok(rows.every((r) => r.open === true));
  assert.strictEqual(rows[1].path, "/v1/catalog/us/albums/al.2/tracks");
  assert.strictEqual(rows[0].path, "/v1/catalog/us/artists/10/view/top-songs");
});

test("an album's tracks come with a Play album row and remember their place in it", async () => {
  const { published, exec } = setup({
    music: { api: { music: () => Promise.resolve({ data: { data: [
      { id: "s1", type: "songs", attributes: { name: "A", artistName: "X" } },
      { id: "s2", type: "songs", attributes: { name: "B", artistName: "X" } }] } }) } },
  });
  exec("library", "/v1/catalog/us/albums/al.1/tracks", "albums", 3);
  await settle();
  const reply = published.find((p) => p.token === 3);
  assert.deepStrictEqual(reply.header, { label: "Play album", detail: "2 songs", type: "albums", id: "al.1" });
  assert.deepStrictEqual(reply.rows.map((r) => [r.label, r.album, r.position]), [["A", "al.1", 0], ["B", "al.1", 1]]);
});

test("playalbum starts the album at the chosen track, and falls back to just that song", async () => {
  const first = setup();
  first.exec("playalbum", JSON.stringify({ album: "al.1", position: 1, song: "s2" }), "", 0);
  await settle();
  assert.deepStrictEqual(first.ops, [["setQueue", { album: "al.1", startWith: 1 }]]);

  const calls = [];
  const fallback = setup({ music: { setQueue: (d) => { calls.push(d); return d.album ? Promise.reject(new Error("no")) : Promise.resolve(); } } });
  fallback.exec("playalbum", JSON.stringify({ album: "al.1", position: 1, song: "s2" }), "", 0);
  await settle();
  assert.deepStrictEqual(calls, [{ album: "al.1", startWith: 1 }, { song: "s2" }]);
});

test("seek tries the newer call first, then the older one", async () => {
  const seen = [];
  const { exec } = setup({ music: { seekToTime: (s) => seen.push(["music", s]) } });
  exec("seek", "42", "", 0);
  await settle();
  assert.deepStrictEqual(seen, [["music", 42]]);
  const older = [];
  const second = setup({ music: { player: { seekToTime: (s) => older.push(["player", s]) } } });
  second.exec("seek", "17", "", 0);
  await settle();
  assert.deepStrictEqual(older, [["player", 17]]);
});

test("volume sets the level on the instance and the player, and a ramp glides there", async () => {
  const player = { volume: 0.5 };
  const { exec, music } = setup({ music: { volume: 0.5, player } });
  global.setInterval = realSetInterval; // the ramp needs a real clock
  exec("volume", "0.2", "", 0);
  await settle();
  assert.strictEqual(music.volume, 0.2);
  assert.strictEqual(player.volume, 0.2);
  exec("volume", "0,80", "", 0);
  await new Promise((r) => realSetTimeout(r, 30));
  assert.ok(music.volume < 0.2 && music.volume > 0, "midway through the ramp: " + music.volume);
  await new Promise((r) => realSetTimeout(r, 120));
  assert.strictEqual(music.volume, 0);
  assert.strictEqual(player.volume, 0);
});

test("only the person's own playlists are offered, never Apple's, and a song can be added to one", async () => {
  const calls = [];
  const lib = [
    { id: "p.mine", type: "library-playlists", attributes: { name: "Gym", canEdit: true, hasCatalog: false, dateAdded: "2026-02-01" } },
    { id: "p.newer", type: "library-playlists", attributes: { name: "Night", canEdit: true, dateAdded: "2026-05-01", description: { standard: "Made in Drift with Grok" } } },
    { id: "p.apple", type: "library-playlists", attributes: { name: "Chill Mix", canEdit: false, hasCatalog: true } },
    { id: "p.added", type: "library-playlists", attributes: { name: "Today's Hits", hasCatalog: true } },
    { id: "pl.editorial", type: "playlists", attributes: { name: "Algorithmic Mix" } },
  ];
  const { published, exec } = setup({ music: { developerToken: "dev", api: { music: (path, params, opts) => {
    calls.push([path, opts && opts.fetchOptions && opts.fetchOptions.method, opts && opts.fetchOptions && opts.fetchOptions.body]);
    return Promise.resolve({ data: { data: /library\/playlists$/.test(path) && !opts ? lib : [] } });
  } } } });
  global.fetch = (url) => Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve({ data: /library\/playlists/.test(url) ? lib : [] }) });
  exec("libraryplaylists", "", "", 0);
  await settle();
  const list = published.find((p) => p.type === "playlists");
  assert.deepStrictEqual(list.items.map((i) => i.id), ["p.newer", "p.mine"]);
  assert.strictEqual(list.items[0].clickMade, true);
  exec("addtoplaylist", JSON.stringify({ playlist: "p.mine", song: "i.abc", name: "Gym" }), "", 0);
  await settle();
  const add = calls.find((c) => /playlists\/p\.mine\/tracks$/.test(c[0]));
  assert.strictEqual(add[1], "POST");
  assert.deepStrictEqual(JSON.parse(add[2]), { data: [{ id: "i.abc", type: "library-songs" }] });
  assert.ok(published.some((p) => p.type === "playlist" && p.state === "added" && p.name === "Gym"));
  exec("newplaylist", JSON.stringify({ name: "3am rain & neon", ids: ["123"] }), "", 0);
  await settle();
  const made = calls.filter((c) => c[1] === "POST").pop();
  assert.strictEqual(JSON.parse(made[2]).attributes.name, "3am rain & neon");
  assert.deepStrictEqual(JSON.parse(made[2]).relationships.tracks.data, [{ id: "123", type: "songs" }]);
});

test("the library's Playlists screen lists every playlist but marks only the person's own", async () => {
  const { published, exec } = setup({ music: { api: { music: () => Promise.resolve({ data: { data: [
    { id: "p.1", type: "library-playlists", attributes: { name: "Mine", canEdit: true } },
    { id: "p.2", type: "library-playlists", attributes: { name: "Apple Made", canEdit: false, hasCatalog: true } },
  ] } }) } } });
  exec("library", "/v1/me/library/playlists", "", 7);
  await settle();
  const rows = published.find((p) => p.type === "rows");
  assert.deepStrictEqual(rows.rows.map((r) => [r.label, !!r.mine]), [["Mine", true], ["Apple Made", false]]);
});

test("love and unlove use the library or catalog rating endpoint to match the song", async () => {
  const seen = [];
  const { published, exec } = setup({ music: { api: { music: (p, q, o) => { seen.push([p, o && o.fetchOptions && o.fetchOptions.method]); return Promise.resolve({ data: { data: [] } }); } } } });
  exec("love", "1234", "", 0);
  exec("unlove", "i.9", "", 0);
  await settle();
  assert.deepStrictEqual(seen, [["/v1/me/ratings/songs/1234", "PUT"], ["/v1/me/ratings/library-songs/i.9", "DELETE"]]);
  assert.ok(published.some((p) => p.type === "rating" && p.id === "1234" && p.loved === true));
});

test("lyrics go through a plain fetch with the page's tokens, never MusicKit's api (which can sign people out)", async () => {
  const asked = [];
  let kitCalls = 0;
  const { published, exec } = setup({ music: {
    developerToken: "dev", musicUserToken: "user",
    api: { music: () => { kitCalls++; return Promise.reject(new Error("must not be used")); } },
  } });
  global.fetch = (url, opts) => {
    const path = url.replace("https://amp-api.music.apple.com", "").split("?")[0];
    asked.push(path);
    assert.strictEqual(opts.headers.Authorization, "Bearer dev");
    assert.strictEqual(opts.headers["Media-User-Token"], "user");
    const ok = (body) => Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve(body) });
    if (/library\/songs\/i\.5$/.test(path)) return ok({ data: [{ id: "i.5", relationships: { catalog: { data: [{ id: "999" }] } } }] });
    if (/songs\/999\/lyrics$/.test(path)) return ok({ data: [{ attributes: { ttml: "<tt>words</tt>" } }] });
    if (/songs\/777\/syllable-lyrics$/.test(path)) return ok({ data: [{ attributes: { ttml: "<tt>word timed</tt>" } }] });
    return Promise.resolve({ ok: false, status: 403, json: () => Promise.resolve({}) });
  };
  exec("lyrics", JSON.stringify({ id: "i.5", title: "T", artist: "A" }), "", 0);
  await settle();
  assert.deepStrictEqual(published.find((p) => p.type === "lyrics"), { type: "lyrics", id: "i.5", text: "<tt>words</tt>" });
  // Word timing is asked for first; this song has none (403), so the line-timed lyrics are used.
  assert.deepStrictEqual(asked, ["/v1/me/library/songs/i.5", "/v1/catalog/us/songs/999/syllable-lyrics", "/v1/catalog/us/songs/999/lyrics"]);
  exec("lyrics", JSON.stringify({ id: "777" }), "", 0);
  await settle();
  assert.deepStrictEqual(published.filter((p) => p.type === "lyrics").pop(), { type: "lyrics", id: "777", text: "<tt>word timed</tt>" });
  exec("lyrics", JSON.stringify({ id: "12345" }), "", 0);
  await settle();
  assert.deepStrictEqual(published.filter((p) => p.type === "lyrics").pop(), { type: "lyrics", id: "12345", text: "" });
  assert.strictEqual(kitCalls, 0);
  delete global.fetch;
});

test("the position keeps its fraction: the playing element is preferred over MusicKit's whole seconds", async () => {
  const { published, exec } = setup({ music: { currentPlaybackTime: 83, currentPlaybackDuration: 214 } });
  document.querySelectorAll = () => [{ paused: false, ended: false, currentTime: 83.64, duration: 214 }];
  exec("noop", "", "", 0);
  await settle();
  assert.strictEqual(published.filter((p) => p.type === "state").pop().position, 83.64);
  // An element far from what MusicKit says (some other media on the page) is ignored.
  document.querySelectorAll = () => [{ paused: false, ended: false, currentTime: 5.5, duration: 30 }];
  exec("noop", "", "", 0);
  await settle();
  assert.strictEqual(published.filter((p) => p.type === "state").pop().position, 83);
});

const fakeSearch = (path, params) => {
  const term = String(params.term);
  const id = "id-" + term.split(" ")[0].toLowerCase();
  return Promise.resolve({ results: { songs: { data: [{ id, attributes: { artistName: term.split(" ").slice(1).join(" ") } }] } } });
};

test("a mix starts with its first song, then the rest join the queue", async () => {
  const { published, ops, exec } = setup({ music: { api: { music: fakeSearch }, playLater: (d) => { ops.push(["playLater", d]); return Promise.resolve(); } } });
  exec("mix", JSON.stringify([{ title: "Alpha", artist: "A" }]), "first", 0);
  await settle();
  assert.deepStrictEqual(ops.find((o) => o[0] === "setQueue")[1], { songs: ["id-alpha"] });
  assert.ok(published.some((p) => p.type === "mix" && p.state === "started"));
  exec("mix", JSON.stringify([{ title: "Beta", artist: "B" }, { title: "Gamma", artist: "C" }]), "more", 0);
  await settle();
  assert.deepStrictEqual(ops.find((o) => o[0] === "playLater")[1], { songs: ["id-beta", "id-gamma"] });
  const done = published.filter((p) => p.type === "mix" && p.state === "done").pop();
  assert.strictEqual(done.more, true);
});

test("a first song that is not on Apple Music says so instead of failing the mix", async () => {
  const { published, exec } = setup({ music: { api: { music: () => Promise.resolve({ results: {} }) } } });
  exec("mix", JSON.stringify([{ title: "Nope", artist: "X" }]), "first", 0);
  await settle();
  assert.ok(published.some((p) => p.type === "mix" && p.state === "firstfailed"));
});

test("restore brings back the song inside its mix, at the saved second, without playing", async () => {
  let played = false;
  let sought = null;
  const { ops, exec } = setup({ music: { play: () => { played = true; return Promise.resolve(); }, seekToTime: (t) => { sought = t; return Promise.resolve(); } } });
  exec("restore", JSON.stringify({ id: "b", position: 91, ids: ["a", "b", "c"] }), "", 0);
  await settle();
  assert.deepStrictEqual(ops.find((o) => o[0] === "setQueue")[1], { songs: ["a", "b", "c"], startWith: 1 });
  assert.strictEqual(sought, 91);
  assert.strictEqual(played, false);
});

test("similar with many: looks up several, adds them to the end of the queue in order, up to the wanted count", async () => {
  const named = (path, params) => fakeSearch(path, params).then((r) => {
    r.results.songs.data.forEach((d) => { d.attributes.name = String(params.term).split(" ")[0]; });
    return r;
  });
  const { published, ops, exec } = setup({ music: { api: { music: named }, playLater: (d) => { ops.push(["playLater", d]); return Promise.resolve(); } } });
  const songs = ["Aa", "Bb", "Cc", "Dd"].map((t) => ({ title: t, artist: "X" }));
  exec("similar", JSON.stringify({ songs, want: 3, many: true }), "", 0);
  await settle(60);
  assert.deepStrictEqual(ops.find((o) => o[0] === "playLater")[1], { songs: ["id-aa", "id-bb", "id-cc"] });
  const added = published.filter((p) => p.type === "similar" && p.state === "added").pop();
  assert.strictEqual(added.items.length, 3);
  assert.strictEqual(added.title, "Aa");
});

(async () => {
  let failed = 0;
  for (const [name, fn] of tests) {
    try {
      await fn();
      console.log("PASS  " + name);
    } catch (error) {
      failed++;
      console.log("FAIL  " + name + "\n      " + String(error.message).split("\n").join("\n      "));
    }
  }
  console.log(`pod.js: ${tests.length - failed} passed, ${failed} failed`);
  process.exit(failed ? 1 : 0);
})();
