// Talks to the MusicKit instance that music.apple.com already signed in.
// Click draws its own screen; this only plays and reports.
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
    var position = 0;
    var duration = 0;
    try { position = finite(music.currentPlaybackTime) || finite(player.currentPlaybackTime) || finite(player.playbackTime); } catch (e) {}
    try {
      duration = finite(music.currentPlaybackDuration) || finite(player.currentPlaybackDuration) || finite(player.playbackDuration) || finite(info.duration);
    } catch (e) { duration = finite(info.duration); }
    var media = null;
    try { media = liveMedia(); } catch (e) {}
    if (media) {
      if (!position) position = finite(media.currentTime);
      if (!duration && isFinite(media.duration)) duration = media.duration;
    }
    var volume = 1;
    try {
      if (typeof player.volume === "number") volume = player.volume;
      else if (typeof music.volume === "number") volume = music.volume;
    } catch (e) {}
    var authorized = false;
    try { authorized = !!music.isAuthorized; } catch (e) {}
    return {
      type: "state",
      ready: true,
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

  function rowsFrom(items) {
    var rows = [];
    (items || []).forEach(function (item) {
      var attributes = item.attributes || {};
      var label = attributes.name || attributes.title || "";
      if (!item.id || !label) return;
      rows.push({
        label: label,
        detail: attributes.artistName || attributes.curatorName || "",
        type: item.type || "",
        id: item.id,
        art: artUrl(attributes.artwork)
      });
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

  window.__podExec = function (cmd, arg, type, token) {
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
        music.player.seekToTime(Math.max(0, Number(arg) || 0));
        done();
      } else if (cmd === "volume") {
        music.player.volume = Math.max(0, Math.min(1, Number(arg)));
        done();
      } else if (cmd === "library") {
        music.api.music(String(arg), { limit: 80 }).then(function (result) {
          publish({ type: "rows", token: token, rows: rowsFrom(bodyItems(result)) });
        }, fail);
      } else if (cmd === "search") {
        var store = music.storefrontId || "us";
        music.api.music("/v1/catalog/" + store + "/search", {
          term: String(arg),
          types: "songs,albums,playlists",
          limit: 20
        }).then(function (result) {
          var results = (result && result.results) || (result && result.data && result.data.results) || {};
          var rows = rowsFrom(results.songs && results.songs.data)
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
