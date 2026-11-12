// Talks to the MusicKit instance that music.apple.com already signed in.
// Drift draws its own screen; this only plays and reports.
(function () {
  if (window.__podInstalled) return;
  window.__podInstalled = true;

  function musicKit() {
    try {
      if (window.MusicKit && window.MusicKit.getInstance) return window.MusicKit.getInstance();
    } catch (e) {}
    return null;
  }

  function artUrl(artwork) {
    if (!artwork) return "";
    if (typeof artwork === "string") return artwork;
    var url = "";
    try { url = artwork.url || artwork.src || ""; } catch (e) { url = ""; }
    if (!url) return "";
    return String(url).replace("{w}", "600").replace("{h}", "600").replace("{f}", "jpg");
  }

  function text(value) {
    if (typeof value === "string") return value.replace(/\s+/g, " ").trim();
    if (typeof value === "number" && isFinite(value)) return String(value);
    return "";
  }

  function finite(value) {
    var n = Number(value);
    return isFinite(n) ? n : 0;
  }

  function readAttrs(item) {
    try {
      if (item && item.attributes && typeof item.attributes === "object") return item.attributes;
    } catch (e) {}
    return {};
  }

  function describe(item) {
    if (!item || typeof item !== "object") return null;
    var attrs = readAttrs(item);
    var play = {};
    try { play = attrs.playParams || item.playParams || {}; } catch (e) { play = {}; }
    return {
      title: text(item.title) || text(item.name) || text(attrs.name) || text(attrs.title),
      artist: text(item.artistName) || text(item.artist) || text(attrs.artistName),
      album: text(item.albumName) || text(item.album) || text(attrs.albumName),
      artwork: artUrl(item.artwork || attrs.artwork || item.artworkURL || attrs.artworkURL),
      id: text(item.id) || text(play.id) || text(item.playbackStoreID) || text(attrs.playParams && attrs.playParams.id),
      duration: finite(item.playbackDuration) || finite(item.duration) || finite(attrs.durationInMillis) / 1000
    };
  }

  function fillMissing(base, extra) {
    var out = base || { title: "", artist: "", album: "", artwork: "", id: "", duration: 0 };
    if (!extra) return out;
    if (!out.title && extra.title) out.title = extra.title;
    if (!out.artist && extra.artist) out.artist = extra.artist;
    if (!out.album && extra.album) out.album = extra.album;
    if (!out.artwork && extra.artwork) out.artwork = extra.artwork;
    if (!out.id && extra.id) out.id = extra.id;
    if (!out.duration && extra.duration) out.duration = extra.duration;
    return out;
  }

  function queueItem(queue, index) {
    if (!queue) return null;
    try {
      if (queue.currentItem) return queue.currentItem;
      var items = queue.items || [];
      if (typeof index === "number" && items[index]) return items[index];
    } catch (e) {}
    return null;
  }

  function currentItem(music, player) {
    var found = null;
    try { found = music.nowPlayingItem; } catch (e) {}
    if (found) return found;
    try { found = player.nowPlayingItem; } catch (e) {}
    if (found) return found;
    var index = null;
    try { index = music.nowPlayingItemIndex; } catch (e) {}
    if (index === undefined || index === null) {
      try { index = player.nowPlayingItemIndex; } catch (e) {}
    }
    var fromPlayer = null;
    var fromMusic = null;
    try { fromPlayer = queueItem(player.queue, index); } catch (e) {}
    try { fromMusic = queueItem(music.queue, index); } catch (e) {}
    return fromPlayer || fromMusic;
  }

  function statePlaying(state) {
    if (state === 2 || state === "playing" || state === "Playing") return true;
    if (state && (state.value === 2 || state.name === "playing" || state.name === "Playing")) return true;
    return false;
  }

  function liveMedia() {
    var nodes;
    try { nodes = document.querySelectorAll("video, audio"); } catch (e) { return null; }
    var best = null;
    for (var i = 0; i < nodes.length; i++) {
      var el = nodes[i];
      if (!el || el.paused || el.ended) continue;
      if (!best || finite(el.currentTime) > finite(best.currentTime)) best = el;
    }
    return best;
  }

  function isPlaying(music, player) {
    try { if (music.isPlaying === true || player.isPlaying === true) return true; } catch (e) {}
    try { if (statePlaying(music.playbackState)) return true; } catch (e) {}
    try { if (statePlaying(player.playbackState)) return true; } catch (e) {}
    try {
      if (navigator.mediaSession && navigator.mediaSession.playbackState === "playing") return true;
    } catch (e) {}
    return !!liveMedia();
  }

  function fromMediaSession() {
    var meta = null;
    try { meta = navigator.mediaSession && navigator.mediaSession.metadata; } catch (e) {}
    if (!meta) return null;
    var art = "";
    try {
      var list = meta.artwork || [];
      for (var i = list.length - 1; i >= 0; i--) {
        if (list[i] && list[i].src) { art = String(list[i].src); break; }
      }
    } catch (e) {}
    var title = text(meta.title);
    if (!title && !text(meta.artist) && !art) return null;
    return { title: title, artist: text(meta.artist), album: text(meta.album), artwork: art, id: "", duration: 0 };
  }

  function fromDocumentTitle() {
    var raw = "";
    try { raw = text(document.title); } catch (e) { return null; }
    if (!raw) return null;
    raw = raw.replace(/\s*[|\u2013\u2014-]\s*Apple Music\s*$/i, "").trim();
    if (!raw || /apple music/i.test(raw) || /^(listen now|browse|radio|search|library|home)$/i.test(raw))
      return null;
    var split = raw.split(/\s+\u2014\s+|\s+\u2013\s+/);
    if (split.length >= 2 && split[0])
      return { title: split[0], artist: split.slice(1).join(" · "), album: "", artwork: "", id: "", duration: 0 };
    if (raw.length > 80) return null;
    return { title: raw, artist: "", album: "", artwork: "", id: "", duration: 0 };
  }

  function arm(music) {
    if (!music || music.__podArmed) return;
    try { music.__podArmed = true; } catch (e) { return; }
    // If MusicKit ever drops the sign-in on its own, leave a note of when and what it had just been doing.
    // Read it with: localStorage.driftAuthLog (in the page's DevTools). Never contains tokens.
    try {
      music.addEventListener("authorizationStatusDidChange", function () {
        var now = false;
        try { now = !!music.isAuthorized; } catch (e) {}
        if (now) return;
        var log = [];
        try { log = JSON.parse(localStorage.driftAuthLog || "[]"); } catch (e) {}
        log.push({ at: new Date().toISOString(), authorized: now, lastCommand: window.__podLastCommand || "" });
        try { localStorage.driftAuthLog = JSON.stringify(log.slice(-20)); } catch (e) {}
        try { console.warn("[drift] MusicKit dropped the sign-in after: " + (window.__podLastCommand || "?")); } catch (e) {}
      });
    } catch (e) {}
    ["playbackStateDidChange", "nowPlayingItemDidChange", "playbackTimeDidChange", "queueItemsDidChange"].forEach(function (name) {
      try {
        music.addEventListener(name, function () { publish(snapshot()); });
      } catch (e) {}
    });
  }

  function watchMedia() {
    var nodes;
    try { nodes = document.querySelectorAll("video, audio"); } catch (e) { return; }
    for (var i = 0; i < nodes.length; i++) {
      var el = nodes[i];
      if (!el || el.__podWatch) continue;
      el.__podWatch = true;
      var nudge = function () { publish(snapshot()); };
      try {
        el.addEventListener("playing", nudge);
        el.addEventListener("pause", nudge);
        el.addEventListener("timeupdate", nudge);
      } catch (e) {}
    }
  }

  function snapshot() {
    var music = musicKit();
    if (!music) return { type: "state", ready: false, authorized: false };
    arm(music);
    watchMedia();
    var player = {};
    try { player = music.player || {}; } catch (e) { player = {}; }
    var playing = false;
    try { playing = isPlaying(music, player); } catch (e) { playing = false; }
    var info = null;
    try { info = describe(currentItem(music, player)); } catch (e) { info = null; }
    try { info = fillMissing(info, fromMediaSession()); } catch (e) {}
    if (playing) {
      try { info = fillMissing(info, fromDocumentTitle()); } catch (e) {}
    }
    info = info || { title: "", artist: "", album: "", artwork: "", id: "", duration: 0 };
    // Songs heard so far, so a similar song is never one that already played.
    try {
      if (info.id) {
        var heard = window.__podPlayed || (window.__podPlayed = []);
        if (heard[heard.length - 1] !== info.id) {
          heard.push(info.id);
          if (heard.length > 60) heard.shift();
        }
      }
    } catch (e) {}
    // How many songs are queued after this one (-1 when the page cannot say).
    var upcoming = -1;
    var queueLength = -1;
    var queuePosition = -1;
    var autoplayState = -1;
    try {
      var queued = (music.queue && music.queue.items) || [];
      var at = Number(music.queue && music.queue.position);
      queueLength = queued.length;
      if (isFinite(at)) queuePosition = at;
      if (isFinite(at) && queued.length) upcoming = Math.max(0, queued.length - at - 1);
    } catch (e) {}
    // Apple's own autoplay adds songs of its own when the queue ends, which would hide the end of
    // the queue from the similar-songs feature. While that is on, it is switched off here (and put back after).
    try {
      if (window.__podNoAutoplay === 1 && music.autoplayEnabled !== false) {
        if (window.__podAutoplayWas === undefined) window.__podAutoplayWas = music.autoplayEnabled;
        music.autoplayEnabled = false;
      } else if (window.__podNoAutoplay === 0 && window.__podAutoplayWas !== undefined) {
        music.autoplayEnabled = window.__podAutoplayWas;
        window.__podAutoplayWas = undefined;
      }
      autoplayState = music.autoplayEnabled === true ? 1 : (music.autoplayEnabled === false ? 0 : -1);
    } catch (e) {}
    var position = 0;
    var duration = 0;
    try { position = finite(music.currentPlaybackTime) || finite(player.currentPlaybackTime) || finite(player.playbackTime); } catch (e) {}
    try {
      duration = finite(music.currentPlaybackDuration) || finite(player.currentPlaybackDuration) || finite(player.playbackDuration) || finite(info.duration);
    } catch (e) { duration = finite(info.duration); }
    var media = null;
    try { media = liveMedia(); } catch (e) {}
    if (media) {
      // MusicKit reports whole seconds; the playing element has the fraction. It is used whenever it agrees
      // with MusicKit to within a couple of seconds (so a stray preview element can never take over).
      var exact = finite(media.currentTime);
      if (exact && (!position || Math.abs(exact - position) < 2.5)) position = exact;
      if (!duration && isFinite(media.duration)) duration = media.duration;
    }
    var volume = volumeNow(music);
    var authorized = false;
    try { authorized = !!music.isAuthorized; } catch (e) {}
    var repeat = 0;
    var shuffle = false;
    try { repeat = Math.max(0, Math.min(2, Number(music.repeatMode) || 0)); } catch (e) {}
    try { shuffle = Number(music.shuffleMode) > 0; } catch (e) {}
    return {
      type: "state",
      ready: true,
      repeat: repeat,
      shuffle: shuffle,
      upcoming: upcoming,
      queueLength: queueLength,
      queuePosition: queuePosition,
      autoplay: autoplayState,
      authorized: authorized,
      playing: playing,
      title: info.title || "",
      artist: info.artist || "",
      album: info.album || "",
      artwork: info.artwork || "",
      trackId: info.id || "",
      position: position,
      duration: duration,
      volume: volume
    };
  }

  // The current level, 0 to 1. The instance holds it in MusicKit v3, the player in older builds.
  function volumeNow(music) {
    try { if (typeof music.volume === "number") return music.volume; } catch (e) {}
    try { if (music.player && typeof music.player.volume === "number") return music.player.volume; } catch (e) {}
    return 1;
  }

  // Playlists the person made or edits themselves. Apple's own (editorial, algorithmic, radio) show up in the
  // library once added, but they are read-only (canEdit false) or backed by the catalog (hasCatalog).
  function isUserPlaylist(item) {
    var a = (item && item.attributes) || {};
    if (a.canEdit === false) return false;
    if (a.canEdit === true) return true;
    return !a.hasCatalog && String(item.id || "").indexOf("p.") === 0;
  }

  // Library songs have ids like "i.abc" and need the library endpoints; catalog ids are plain numbers.
  function isLibraryId(id) { return /^[il]\./.test(String(id)); }
  function songType(id) { return isLibraryId(id) ? "library-songs" : "songs"; }

  // A read from Apple's API that cannot sign anyone out. MusicKit's own `api.music` treats some refusals as a
  // dead session and drops the sign-in; optional extras (lyrics) go through a plain fetch with the same tokens
  // instead, so a 401/403/404 there is only "no lyrics".
  function quietRequest(music, method, path, params, body) {
    var dev = "", user = "";
    try { dev = music.developerToken || ""; user = music.musicUserToken || ""; } catch (e) {}
    if (!dev || typeof fetch !== "function") return Promise.reject(new Error("no token"));
    var query = "";
    if (params) {
      var parts = [];
      for (var k in params) parts.push(encodeURIComponent(k) + "=" + encodeURIComponent(params[k]));
      if (parts.length) query = "?" + parts.join("&");
    }
    var headers = { Authorization: "Bearer " + dev };
    if (user) headers["Media-User-Token"] = user;
    var options = { method: method, headers: headers, credentials: "omit" };
    if (body !== undefined) { headers["Content-Type"] = "application/json"; options.body = JSON.stringify(body); }
    return fetch("https://amp-api.music.apple.com" + path + query, options).then(function (response) {
      if (!response.ok) throw new Error("HTTP " + response.status);
      if (method === "GET") return response.json();
      // DELETE and some POSTs answer with no body.
      return response.text().then(function (text) { try { return text ? JSON.parse(text) : {}; } catch (e) { return {}; } });
    });
  }
  function quietGet(music, path, params) { return quietRequest(music, "GET", path, params); }

  function jsonBody(method, body) {
    return { fetchOptions: { method: method, headers: { "Content-Type": "application/json" }, body: body === undefined ? undefined : JSON.stringify(body) } };
  }

  // `decorate(item, row, position)` may change a row, e.g. to say it opens another screen.
  function rowsFrom(items, decorate) {
    var rows = [];
    (items || []).forEach(function (item) {
      var attributes = item.attributes || {};
      var label = attributes.name || attributes.title || "";
      if (!item.id || !label) return;
      var row = {
        label: label,
        detail: attributes.artistName || attributes.curatorName || "",
        type: item.type || "",
        id: item.id,
        art: artUrl(attributes.artwork)
      };
      if (decorate) decorate(item, row, rows.length);
      rows.push(row);
    });
    return rows;
  }

  var lastState = 0;
  function publish(payload, force) {
    if (!force && payload && payload.type === "state") {
      var now = Date.now();
      if (now - lastState < 250) return;
      lastState = now;
    } else if (payload && payload.type === "state") {
      lastState = Date.now();
    }
    if (window.pod && window.pod.ingest) window.pod.ingest(JSON.stringify(payload));
  }

  function burst() {
    var left = 8;
    var tick = function () {
      try { publish(snapshot(), true); } catch (e) {}
      if (--left > 0) setTimeout(tick, 350);
    };
    tick();
  }

  function bodyItems(result) {
    if (!result) return [];
    if (result.data && result.data.data) return result.data.data;
    if (result.data) return result.data;
    return [];
  }

  // Newer MusicKit seeks on the instance, older builds on its player, and if
  // neither lands the playing media element is moved directly.
  function seekTo(music, seconds) {
    var asked = false;
    try {
      if (typeof music.seekToTime === "function") { music.seekToTime(seconds); asked = true; }
    } catch (e) {}
    if (!asked) {
      try {
        if (music.player && typeof music.player.seekToTime === "function") { music.player.seekToTime(seconds); asked = true; }
      } catch (e) {}
    }
    // Check shortly after: if the position did not move to where it was asked, set the element itself.
    setTimeout(function () {
      var now = -1;
      try { now = finite(music.currentPlaybackTime); } catch (e) {}
      if (Math.abs(now - seconds) <= 4) return;
      var media = null;
      try { media = liveMedia() || document.querySelector("audio, video"); } catch (e) {}
      try { if (media) media.currentTime = seconds; } catch (e) {}
    }, 600);
  }

  window.__podExec = function (cmd, arg, type, token) {
    if (cmd !== "seek" && cmd !== "volume") window.__podLastCommand = String(cmd) + " " + new Date().toISOString();
    if (cmd === "authorize") {
      if (window.__podAuthorizing) return;
      window.__podAuthorizing = true;
      var tries = 0;
      var attempt = function () {
        var kit = musicKit();
        if (!kit) {
          if (++tries < 40) {
            setTimeout(attempt, 500);
            return;
          }
          window.__podAuthorizing = false;
          publish({ type: "state", ready: false, authorized: false, error: "Apple Music did not load." });
          return;
        }
        kit.authorize().then(function () {
          window.__podAuthorizing = false;
          publish(snapshot());
        }, function (error) {
          window.__podAuthorizing = false;
          publish({ type: "state", ready: true, authorized: !!kit.isAuthorized, error: String(error && error.message || error) });
        });
      };
      attempt();
      return;
    }

    var music = musicKit();
    if (!music) {
      publish({ type: "state", ready: false, authorized: false });
      return;
    }
    var done = function () { burst(); };
    var fail = function (error) {
      publish({ type: "state", ready: true, authorized: !!music.isAuthorized, error: String(error && error.message || error) });
    };
    try {
      if (cmd === "toggle") {
        var playingNow = false;
        try { playingNow = isPlaying(music, music.player || {}); } catch (e) {}
        var action = playingNow ? music.pause() : music.play();
        Promise.resolve(action).then(done, fail);
      } else if (cmd === "next") {
        Promise.resolve(music.skipToNextItem()).then(done, fail);
      } else if (cmd === "prev") {
        Promise.resolve(music.skipToPreviousItem()).then(done, fail);
      } else if (cmd === "seek") {
        seekTo(music, Math.max(0, Number(arg) || 0));
        done();
      } else if (cmd === "volume") {
        // "target" or "target,rampMs". A short ramp (mute, unmute) steps the level so it never clicks.
        var vparts = String(arg).split(",");
        var vtarget = Math.max(0, Math.min(1, Number(vparts[0])));
        var vramp = Math.max(0, Number(vparts[1]) || 0);
        var setLevel = function (value) {
          // MusicKit v3 keeps the level on the instance; older builds on the player. Set whichever exist.
          try { music.volume = value; } catch (e) {}
          try { if (music.player) music.player.volume = value; } catch (e) {}
        };
        if (window.__podVolumeTimer) { clearInterval(window.__podVolumeTimer); window.__podVolumeTimer = 0; }
        var vfrom = Number(volumeNow(music));
        if (!vramp || !isFinite(vfrom) || Math.abs(vfrom - vtarget) < 0.01) {
          setLevel(vtarget);
        } else {
          var vstart = Date.now();
          window.__podVolumeTimer = setInterval(function () {
            var t = Math.min(1, (Date.now() - vstart) / vramp);
            setLevel(vfrom + (vtarget - vfrom) * t);
            if (t >= 1) { clearInterval(window.__podVolumeTimer); window.__podVolumeTimer = 0; }
          }, 8);
        }
        done();
      } else if (cmd === "repeat") {
        // MusicKit.PlayerRepeatMode: 0 none, 1 one, 2 all.
        music.repeatMode = Math.max(0, Math.min(2, Number(arg) || 0));
        done();
      } else if (cmd === "shuffle") {
        // MusicKit.PlayerShuffleMode: 0 off, 1 songs.
        music.shuffleMode = Number(arg) > 0 ? 1 : 0;
        done();
      } else if (cmd === "library") {
        // An album's tracks play as that album, from the track that was chosen, and come with a "Play album" row.
        var albumPath = String(arg).match(/\/albums\/([^\/]+)\/tracks$/);
        var isPlaylistList = /\/me\/library\/playlists$/.test(String(arg));
        music.api.music(String(arg), { limit: isPlaylistList ? 100 : 80 }).then(function (result) {
          var found = bodyItems(result);
          var rows = rowsFrom(found, albumPath ? function (item, row, position) {
            row.album = albumPath[1];
            row.position = position;
          } : (isPlaylistList ? function (item, row) {
            // Apple's own stay listed (the person saved them and may want to play them); only theirs get `mine`.
            row.mine = isUserPlaylist(item);
          } : null));
          var header = null;
          if (albumPath && rows.length) {
            header = {
              label: "Play album",
              detail: rows.length + (rows.length === 1 ? " song" : " songs"),
              type: String(arg).indexOf("/me/library/") !== -1 ? "library-albums" : "albums",
              id: albumPath[1]
            };
          }
          publish({ type: "rows", token: token, rows: rows, header: header });
        }, fail);
      } else if (cmd === "artist") {
        // An artist's page: their top songs, then their albums (newest first, over a few pages).
        var artistId = String(arg);
        var artistStore = music.storefrontId || "us";
        var artistBase = "/v1/catalog/" + artistStore + "/artists/" + artistId;
        var gathered = [];
        var fetchPage = function (path, params, left) {
          return music.api.music(path, params).then(function (result) {
            var body = (result && result.data) || {};
            gathered = gathered.concat(body.data || []);
            if (body.next && left > 1 && gathered.length < 100) return fetchPage(body.next, undefined, left - 1);
          });
        };
        fetchPage(artistBase + "/albums", { limit: 25 }, 4).then(function () {
          gathered.sort(function (a, b) {
            var x = String((a.attributes || {}).releaseDate || "");
            var y = String((b.attributes || {}).releaseDate || "");
            return x < y ? 1 : (x > y ? -1 : 0);
          });
          var rows = [{ label: "Top Songs", detail: "", type: "views", id: artistBase + "/view/top-songs", open: true,
                        path: artistBase + "/view/top-songs" }];
          rows = rows.concat(rowsFrom(gathered, function (item, row) {
            var a = item.attributes || {};
            var year = String(a.releaseDate || "").slice(0, 4);
            var songs = a.trackCount ? a.trackCount + (a.trackCount === 1 ? " song" : " songs") : "";
            row.detail = year && songs ? year + " · " + songs : (year || songs || row.detail);
            row.open = true;
            row.path = "/v1/catalog/" + artistStore + "/albums/" + item.id + "/tracks";
          }));
          publish({ type: "rows", token: token, rows: rows });
        }, fail);
      } else if (cmd === "playalbum") {
        // Starts an album at one of its tracks; if that cannot be done, plays just that song.
        var spec = {};
        try { spec = JSON.parse(String(arg)) || {}; } catch (e) {}
        var albumQueue = { album: String(spec.album) };
        if (isFinite(Number(spec.position))) albumQueue.startWith = Number(spec.position);
        music.setQueue(albumQueue).then(function () { return music.play(); }).then(done, function (error) {
          if (!spec.song) return fail(error);
          return music.setQueue({ song: String(spec.song) }).then(function () { return music.play(); }).then(done, fail);
        });
      } else if (cmd === "search") {
        var store = music.storefrontId || "us";
        music.api.music("/v1/catalog/" + store + "/search", {
          term: String(arg),
          types: "artists,songs,albums,playlists",
          limit: 20
        }).then(function (result) {
          var results = (result && result.results) || (result && result.data && result.data.results) || {};
          // The artists come first (a few of them), so an artist's albums are one step away.
          var rows = rowsFrom(((results.artists && results.artists.data) || []).slice(0, 3), function (item, row) { row.detail = "Artist"; })
            .concat(rowsFrom(results.songs && results.songs.data))
            .concat(rowsFrom(results.albums && results.albums.data))
            .concat(rowsFrom(results.playlists && results.playlists.data));
          publish({ type: "rows", token: token, rows: rows });
        }, fail);
      } else if (cmd === "play") {
        var kind = String(type || "");
        var descriptor = kind.indexOf("album") !== -1 ? { album: String(arg) }
          : kind.indexOf("playlist") !== -1 ? { playlist: String(arg) }
          : { song: String(arg) };
        music.setQueue(descriptor).then(function () { return music.play(); }).then(done, fail);
      } else if (cmd === "mix") {
        // type "first": look up and play one song at once (Grok is still writing the rest);
        // "more": look up the rest and add them to the end of what is already playing;
        // anything else: the whole mix in one go.
        var mixPart = String(type || "");
        var wanted = [];
        try { wanted = JSON.parse(String(arg)) || []; } catch (e) {}
        var mixStore = music.storefrontId || "us";
        var picked = new Array(wanted.length);
        var looked = 0;
        var cursor = 0;
        var lookup = function (index) {
          var want = wanted[index] || {};
          var term = text((want.title || "") + " " + (want.artist || ""));
          return music.api.music("/v1/catalog/" + mixStore + "/search", { term: term, types: "songs", limit: 5 }).then(function (result) {
            var results = (result && result.results) || (result && result.data && result.data.results) || {};
            var data = (results.songs && results.songs.data) || [];
            var artist = String(want.artist || "").toLowerCase();
            var best = null;
            data.forEach(function (item) {
              var a = String((item.attributes && item.attributes.artistName) || "").toLowerCase();
              if (!best && artist && (a.indexOf(artist) !== -1 || artist.indexOf(a) !== -1)) best = item;
            });
            picked[index] = (best || data[0] || {}).id || "";
          }, function () { picked[index] = ""; }).then(function () {
            looked++;
            if (mixPart !== "first") publish({ type: "mix", state: "progress", found: looked, total: wanted.length });
          });
        };
        var worker = function () {
          if (cursor >= wanted.length) return Promise.resolve();
          return lookup(cursor++).then(worker);
        };
        var pool = [];
        for (var w = 0; w < Math.min(4, wanted.length); w++) pool.push(worker());
        Promise.all(pool).then(function () {
          var ids = [];
          picked.forEach(function (id) { if (id && ids.indexOf(id) === -1) ids.push(id); });
          if (!ids.length) {
            publish({ type: "mix", state: mixPart === "first" ? "firstfailed" : "error", message: "None of those songs were found on Apple Music." });
            return;
          }
          if (mixPart === "more") {
            var hasQueue = false;
            try { hasQueue = !!music.nowPlayingItem && (music.queue.items || []).length > 0; } catch (e) {}
            var added = hasQueue ? music.playLater({ songs: ids }) : music.setQueue({ songs: ids }).then(function () { return music.play(); });
            return Promise.resolve(added).then(function () {
              publish({ type: "mix", state: "done", more: true, ids: ids });
              done();
            });
          }
          return music.setQueue({ songs: ids }).then(function () { return music.play(); }).then(function () {
            publish({ type: "mix", state: mixPart === "first" ? "started" : "done", ids: ids });
            done();
          });
        }).catch(function (error) {
          publish({ type: "mix", state: mixPart === "first" ? "firstfailed" : "error", message: String(error && error.message || error) });
        });
      } else if (cmd === "restore") {
        // Bring back what was playing when the app was closed: the song (inside its mix, if it came from one),
        // paused, at the second it was left. Never starts the music by itself.
        var snap = {};
        try { snap = JSON.parse(String(arg)) || {}; } catch (e) {}
        var snapIds = snap.ids || [];
        var at = snapIds.indexOf(String(snap.id));
        var snapQueue = at >= 0 ? { songs: snapIds, startWith: at } : { song: String(snap.id) };
        music.setQueue(snapQueue).then(function () {
          return snap.position > 3 ? music.seekToTime(snap.position) : null;
        }).then(done, fail);
      } else if (cmd === "libraryplaylists") {
        // The playlists the add-to menu offers: the person's own, newest first, never Apple's.
        var mine = [];
        var pageNo = 0;
        var nextPage = function (offset) {
          return quietGet(music, "/v1/me/library/playlists", { limit: 100, offset: offset }).then(function (result) {
            var page = bodyItems(result);
            page.forEach(function (item) { if (isUserPlaylist(item)) mine.push(item); });
            if (page.length >= 100 && ++pageNo < 5) return nextPage(offset + 100);
          });
        };
        nextPage(0).then(function () {
          mine.sort(function (a, b) {
            return String((b.attributes || {}).dateAdded || "").localeCompare(String((a.attributes || {}).dateAdded || ""));
          });
          publish({ type: "playlists", items: mine.map(function (item) {
            var attrs = item.attributes || {};
            return { id: String(item.id), name: text(attrs.name || ""), clickMade: /Made in (Click|Drift)/.test(attrs.description && attrs.description.standard || attrs.description || "") };
          }) });
        }, function (error) {
          publish({ type: "playlist", state: "error", message: "Could not read your playlists: " + String(error && error.message || error) });
        });
      } else if (cmd === "addtoplaylist") {
        var addReq = {};
        try { addReq = JSON.parse(String(arg)) || {}; } catch (e) {}
        quietRequest(music, "POST", "/v1/me/library/playlists/" + encodeURIComponent(addReq.playlist) + "/tracks", null,
          { data: [{ id: String(addReq.song), type: songType(addReq.song) }] })
          .then(function () { publish({ type: "playlist", state: "added", name: addReq.name || "" }); },
                function (error) { publish({ type: "playlist", state: "error", message: "Could not add: " + String(error && error.message || error) }); });
      } else if (cmd === "newplaylist") {
        var newReq = {};
        try { newReq = JSON.parse(String(arg)) || {}; } catch (e) {}
        var newBody = {
          attributes: { name: String(newReq.name || "New playlist"), description: String(newReq.description || "Made in Drift") },
          relationships: { tracks: { data: (newReq.ids || []).map(function (id) { return { id: String(id), type: songType(id) }; }) } }
        };
        quietRequest(music, "POST", "/v1/me/library/playlists", null, newBody)
          .then(function (result) {
            var made = bodyItems(result)[0] || {};
            publish({ type: "playlist", state: "created", name: newBody.attributes.name, id: String(made.id || "") });
          }, function (error) { publish({ type: "playlist", state: "error", message: "Could not create: " + String(error && error.message || error) }); });
      } else if (cmd === "lyrics") {
        // Apple's own lyrics for the catalog song, as TTML (line- or word-timed). Library songs ("i.abc") are
        // first mapped to their catalog song. Anything that goes wrong is "no lyrics", never an error toast.
        var lyricReq = {};
        try { lyricReq = JSON.parse(String(arg)) || {}; } catch (e) {}
        var lyricId = String(lyricReq.id || "");
        var lyricStore = music.storefrontId || "us";
        var catalogId = isLibraryId(lyricId)
          ? quietGet(music, "/v1/me/library/songs/" + encodeURIComponent(lyricId), { include: "catalog" }).then(function (result) {
              var item = bodyItems(result)[0] || {};
              var cat = item.relationships && item.relationships.catalog && item.relationships.catalog.data && item.relationships.catalog.data[0];
              return cat ? String(cat.id) : "";
            })
          : Promise.resolve(lyricId);
        // Word-by-word timing lives on "syllable-lyrics"; "lyrics" only times whole lines. The first is tried
        // first, and the second is the fallback for songs that have no word timing.
        var ttmlOf = function (result) {
          var entry = result ? bodyItems(result)[0] : null;
          var lattrs = (entry && entry.attributes) || {};
          return String(lattrs.ttml || lattrs.ttmlLocalizations || "");
        };
        catalogId.then(function (cid) {
          if (!cid) return "";
          var base = "/v1/catalog/" + lyricStore + "/songs/" + encodeURIComponent(cid);
          return quietGet(music, base + "/syllable-lyrics").then(ttmlOf, function () { return ""; }).then(function (words) {
            if (words) return words;
            return quietGet(music, base + "/lyrics").then(ttmlOf);
          });
        }).then(function (text) {
          publish({ type: "lyrics", id: lyricId, text: text || "" });
        }, function () { publish({ type: "lyrics", id: lyricId, text: "" }); });
      } else if (cmd === "love" || cmd === "unlove") {
        // Apple Music's own heart: a rating of 1 on the catalog song.
        var loveId = String(arg);
        var loved = cmd === "love"
          ? quietRequest(music, "PUT", "/v1/me/ratings/" + songType(loveId) + "/" + encodeURIComponent(loveId), null, { type: "rating", attributes: { value: 1 } })
          : quietRequest(music, "DELETE", "/v1/me/ratings/" + songType(loveId) + "/" + encodeURIComponent(loveId));
        Promise.resolve(loved).then(function () { publish({ type: "rating", id: loveId, loved: cmd === "love" }); },
                                    function (error) { publish({ type: "playlist", state: "error", message: "Could not save the like: " + String(error && error.message || error) }); });
      } else if (cmd === "rating") {
        var rateId = String(arg);
        quietGet(music, "/v1/me/ratings/" + songType(rateId) + "/" + encodeURIComponent(rateId)).then(function (result) {
          var got = bodyItems(result)[0];
          publish({ type: "rating", id: rateId, loved: !!got && Number((got.attributes || {}).value) > 0 });
        }, function () { publish({ type: "rating", id: rateId, loved: false }); });
      } else if (cmd === "savemix") {
        var saveReq = {};
        try { saveReq = JSON.parse(String(arg)) || {}; } catch (e) {}
        var body = {
          attributes: { name: String(saveReq.name || "Drift Mix"), description: "Made in Drift with Grok" },
          relationships: { tracks: { data: (saveReq.ids || []).map(function (id) { return { id: String(id), type: "songs" }; }) } }
        };
        quietRequest(music, "POST", "/v1/me/library/playlists", null, body)
          .then(function () { publish({ type: "mix", state: "saved" }); },
                function (error) { publish({ type: "mix", state: "error", message: "Could not save: " + String(error && error.message || error) }); });
      } else if (cmd === "playnext" || cmd === "playlater") {
        var qkind = String(type || "");
        var qdesc = qkind.indexOf("album") !== -1 ? { album: String(arg) }
          : qkind.indexOf("playlist") !== -1 ? { playlist: String(arg) }
          : { song: String(arg) };
        var hasQueue = false;
        try { hasQueue = !!music.nowPlayingItem && (music.queue.items || []).length > 0; } catch (e) {}
        var queued = hasQueue ? (cmd === "playnext" ? music.playNext(qdesc) : music.playLater(qdesc))
          : music.setQueue(qdesc).then(function () { return music.play(); });
        Promise.resolve(queued).then(done, fail);
      } else if (cmd === "upnext") {
        var qitems = [];
        var qpos = -1;
        try { qitems = music.queue.items || []; qpos = Number(music.queue.position); } catch (e) {}
        if (!isFinite(qpos)) qpos = -1;
        var upcoming = [];
        for (var qi = qpos + 1; qi < qitems.length; qi++) {
          var qit = qitems[qi] || {};
          var qattr = qit.attributes || {};
          var qlabel = text(qit.title || qattr.name || "");
          if (!qlabel) continue;
          upcoming.push({ label: qlabel, detail: text(qit.artistName || qattr.artistName || ""), type: "queueitem", id: String(qi) });
        }
        publish({ type: "rows", token: token, rows: upcoming });
      } else if (cmd === "similar") {
        // Candidates, best first. One song is queued right after the current one; with "many", up to `want` of them
        // are looked up together and added to the end of the queue, in order.
        var wish = {};
        try { wish = JSON.parse(String(arg)) || {}; } catch (e) {}
        var candidates = Array.isArray(wish.songs) ? wish.songs : (wish.title ? [wish] : []);
        var wishStore = music.storefrontId || "us";
        var wantMany = !!wish.many;
        var wantCount = wantMany ? Math.max(1, Math.min(12, Number(wish.want) || 1)) : 1;
        var lookupOne = function (candidate) {
          return music.api.music("/v1/catalog/" + wishStore + "/search", {
            term: text((candidate.title || "") + " " + (candidate.artist || "")), types: "songs", limit: 5
          }).then(function (result) {
            var found = (result && result.results) || (result && result.data && result.data.results) || {};
            var songs = (found.songs && found.songs.data) || [];
            var heard = window.__podPlayed || [];
            var wanted = String(candidate.artist || "").toLowerCase();
            var pick = null;
            songs.forEach(function (item) {
              if (pick || heard.indexOf(String(item.id)) !== -1) return;
              var who = String((item.attributes && item.attributes.artistName) || "").toLowerCase();
              if (!wanted || who.indexOf(wanted) !== -1 || wanted.indexOf(who) !== -1) pick = item;
            });
            return pick;
          }, function () { return null; });
        };
        var inQueue = false;
        var finish = function (picks) {
          if (!picks.length) {
            publish({ type: "similar", state: "error", message: "None of the suggested songs were on Apple Music." });
            return Promise.resolve();
          }
          try { inQueue = !!music.nowPlayingItem && (music.queue.items || []).length > 0; } catch (e) {}
          var ids = picks.map(function (p) { return String(p.id); });
          var added;
          if (wantMany) {
            added = inQueue ? music.playLater({ songs: ids })
              : music.setQueue({ songs: ids }).then(function () { return music.play(); });
          } else {
            // Right after the song that is playing, so it is what comes next.
            added = inQueue ? music.playNext({ song: ids[0] })
              : music.setQueue({ song: ids[0] }).then(function () { return music.play(); });
          }
          return Promise.resolve(added).then(function () {
            var items = picks.map(function (p) {
              var shown = p.attributes || {};
              return { title: text(shown.name), artist: text(shown.artistName), id: String(p.id) };
            });
            publish({ type: "similar", state: "added", title: items[0].title, artist: items[0].artist, id: items[0].id, items: items });
            done();
          });
        };
        var chosen = [];
        var tried = 0;
        var worker = function () {
          if (tried >= candidates.length || chosen.length >= wantCount) return Promise.resolve();
          var at = tried++;
          return lookupOne(candidates[at] || {}).then(function (pick) {
            if (pick && !chosen.some(function (c) { return String(c.id) === String(pick.id); })) chosen[at] = pick;
          }).then(worker);
        };
        var lanes = [];
        for (var lane = 0; lane < Math.min(wantMany ? 4 : 1, candidates.length); lane++) lanes.push(worker());
        Promise.all(lanes).then(function () {
          var picks = chosen.filter(function (c) { return !!c; }).slice(0, wantCount);
          return finish(picks);
        }).catch(function (error) {
          publish({ type: "similar", state: "error", message: String(error && error.message || error) });
        });
      } else if (cmd === "jump") {
        Promise.resolve(music.changeToMediaAtIndex(Number(arg))).then(done, fail);
      } else {
        done();
      }
    } catch (error) {
      fail(error);
    }
  };

  function connect() {
    if (window.pod) return;
    if (typeof QWebChannel !== "function" || !window.qt || !window.qt.webChannelTransport) {
      setTimeout(connect, 250);
      return;
    }
    new QWebChannel(window.qt.webChannelTransport, function (channel) {
      window.pod = channel.objects.bridge;
      publish(snapshot());
      if (window.__podWantAuth) window.__podExec("authorize", "", "", 0);
    });
  }

  connect();
  if (window.__podWantAuth) window.__podExec("authorize", "", "", 0);
  setInterval(function () {
    if (window.pod) publish(snapshot());
  }, 700);
})();
