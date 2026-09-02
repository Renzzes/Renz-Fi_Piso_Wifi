import { useCallback, useEffect, useRef, useState } from "react";
import { ImageIcon } from "lucide-react";
import "./captive-portal-preview.css";

export type CaptivePortalLivePreviewProps = {
  bannerSrc: string | null;
  isVideo: boolean;
  musicSrc: string;
};

/** Natural design width of portal-shell in preview CSS (matches captive-portal-preview.css). */
const PORTAL_DESIGN_WIDTH = 520;
/** Horizontal bleed for HUD brackets/borders that extend outside the shell width. */
const PREVIEW_EDGE_BLEED = 14;
const VIEWPORT_PAD = 16;

const COIN_ICON = (
  <svg viewBox="0 0 28 24" fill="none" aria-hidden>
    <ellipse cx="14" cy="18.6" rx="8.2" ry="3.15" fill="currentColor" opacity="0.45" />
    <ellipse cx="14" cy="15.2" rx="8.2" ry="3.15" fill="currentColor" opacity="0.65" />
    <ellipse cx="14" cy="11.8" rx="8.2" ry="3.15" fill="currentColor" opacity="0.85" />
    <ellipse cx="14" cy="8.3" rx="8.2" ry="3.15" fill="currentColor" />
    <ellipse
      cx="14"
      cy="8.3"
      rx="8.2"
      ry="3.15"
      stroke="currentColor"
      strokeWidth="1.15"
      fill="none"
    />
  </svg>
);

function formatTimer(totalSeconds: number): string {
  const h = Math.floor(totalSeconds / 3600);
  const m = Math.floor((totalSeconds % 3600) / 60);
  const s = totalSeconds % 60;
  return [h, m, s].map((n) => String(n).padStart(2, "0")).join(":");
}

/** Visual shell mirroring portal/login.html — banner is real; coin flow is preview-only. */
export function CaptivePortalLivePreview({
  bannerSrc,
  isVideo,
  musicSrc,
}: CaptivePortalLivePreviewProps) {
  const audioRef = useRef<HTMLAudioElement | null>(null);
  const timerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const viewportRef = useRef<HTMLDivElement | null>(null);
  const contentRef = useRef<HTMLDivElement | null>(null);
  const [scale, setScale] = useState(1);
  const [contentHeight, setContentHeight] = useState(0);
  const [ready, setReady] = useState(false);
  const [coinActive, setCoinActive] = useState(false);
  const [secondsLeft, setSecondsLeft] = useState(0);
  const [credits, setCredits] = useState("0.00");

  const previewDate = new Intl.DateTimeFormat(undefined, {
    weekday: "short",
    year: "numeric",
    month: "short",
    day: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  }).format(new Date());

  const stopTimer = useCallback(() => {
    if (timerRef.current) {
      clearInterval(timerRef.current);
      timerRef.current = null;
    }
  }, []);

  useEffect(() => () => stopTimer(), [stopTimer]);

  const recomputeScale = useCallback(() => {
    const viewport = viewportRef.current;
    const content = contentRef.current;
    if (!viewport || !content) return;

    const viewportWidth = viewport.clientWidth;
    const viewportHeight = viewport.clientHeight;
    const measuredWidth = Math.max(
      content.scrollWidth,
      content.offsetWidth,
      PORTAL_DESIGN_WIDTH + PREVIEW_EDGE_BLEED * 2,
    );
    const measuredHeight = content.scrollHeight;
    if (viewportWidth <= 0 || viewportHeight <= 0 || measuredWidth <= 0 || measuredHeight <= 0) {
      return;
    }

    const widthScale =
      (viewportWidth - VIEWPORT_PAD * 2) / (measuredWidth + PREVIEW_EDGE_BLEED);
    const heightScale = (viewportHeight - VIEWPORT_PAD * 2) / measuredHeight;
    const nextScale = Math.min(widthScale, heightScale, 1);
    setScale(nextScale);
    setContentHeight(measuredHeight);
    setReady(true);
  }, []);

  useEffect(() => {
    const viewport = viewportRef.current;
    const content = contentRef.current;
    if (!viewport || !content) return;

    const measure = () => {
      recomputeScale();
      requestAnimationFrame(recomputeScale);
    };

    measure();

    const viewportObserver = new ResizeObserver(measure);
    const contentObserver = new ResizeObserver(measure);
    viewportObserver.observe(viewport);
    contentObserver.observe(content);

    return () => {
      viewportObserver.disconnect();
      contentObserver.disconnect();
    };
  }, [recomputeScale, bannerSrc, isVideo, coinActive]);

  const scaledWidth = (PORTAL_DESIGN_WIDTH + PREVIEW_EDGE_BLEED * 2) * scale;
  const scaledHeight = contentHeight * scale;

  const handleInsertCoin = () => {
    setCoinActive(true);
    setCredits("5.00");
    setSecondsLeft(3600);
    stopTimer();
    timerRef.current = setInterval(() => {
      setSecondsLeft((prev) => (prev > 0 ? prev - 1 : 0));
    }, 1000);

    const audio = audioRef.current;
    if (audio) {
      audio.currentTime = 0;
      void audio.play().catch(() => {
        /* Autoplay blocked without gesture — click provides gesture; ignore otherwise. */
      });
    }
  };

  return (
    <div className="portal-live-preview min-w-0" aria-label="Captive portal live preview">
      <audio ref={audioRef} src={musicSrc} loop preload="none" className="sr-only" />
      <div ref={viewportRef} className="portal-live-preview__viewport">
        <div
          className="portal-live-preview__fit-host"
          style={{
            width: ready ? scaledWidth : undefined,
            height: ready ? scaledHeight : undefined,
            opacity: ready ? 1 : 0,
          }}
        >
          <div
            ref={contentRef}
            className="portal-live-preview__fit-inner"
            style={{
              width: PORTAL_DESIGN_WIDTH,
              transform: `scale(${scale})`,
              transformOrigin: "top left",
            }}
          >
            <div className="portal-live-preview__backdrop">
              <main className="portal-shell">
                <header
                  className={`hero portal-banner${bannerSrc ? "" : " branding-pending"}`}
                  id="portalHeroPreview"
                >
                  {bannerSrc ? (
                    isVideo ? (
                      <video
                        className="portal-logo banner-active"
                        src={bannerSrc}
                        autoPlay
                        muted
                        loop
                        playsInline
                        aria-label="Portal banner preview"
                        onLoadedData={recomputeScale}
                      />
                    ) : (
                      <img
                        className="portal-logo banner-active"
                        src={bannerSrc}
                        alt="Renz-Fi portal banner preview"
                        decoding="async"
                        onLoad={recomputeScale}
                      />
                    )
                  ) : (
                    <div className="banner-upload-placeholder" aria-label="No banner uploaded">
                      <ImageIcon className="banner-upload-placeholder__icon" aria-hidden />
                      <p className="banner-upload-placeholder__title">Upload a banner</p>
                      <p className="banner-upload-placeholder__hint">
                        Choose a banner file to preview it here
                      </p>
                    </div>
                  )}
                  <span className="banner-frame" aria-hidden="true" />
                </header>

                <section
                  className={`hud-panel status-panel${coinActive ? " status-panel--connected" : ""}`}
                >
                  <p className="hud-kicker">STATUS</p>
                  <div className="status-readout">
                    <span
                      className={`status-pulse${coinActive ? " status-pulse--connected" : ""}`}
                    />
                    <strong
                      className={`status-label${coinActive ? " status-label--connected" : ""}`}
                    >
                      {coinActive ? "Connected" : "Disconnected"}
                    </strong>
                  </div>
                  <small className="preview-date">{previewDate}</small>
                </section>

                <section className="hud-panel device-card" aria-hidden="true">
                  <article className="info-item">
                    <p>IP ADDRESS</p>
                    <strong>192.168.88.100</strong>
                  </article>
                  <article className="info-item">
                    <p>MAC ADDRESS</p>
                    <strong>AA:BB:CC:DD:EE:FF</strong>
                  </article>
                  <article className="info-item credits-item">
                    <p>ACCOUNT CREDITS</p>
                    <strong>&#8369;{credits}</strong>
                  </article>
                </section>

                <section className="hud-panel timer-card" aria-hidden="true">
                  <p className="hud-kicker">REMAINING TIME</p>
                  <div className="timer-value">{formatTimer(secondsLeft)}</div>
                  <div className="timer-labels">
                    <span>HRS</span>
                    <span>MINS</span>
                    <span>SECS</span>
                  </div>
                </section>

                <section className="hud-panel package-panel" aria-hidden="true">
                  <p className="hud-kicker">CHOOSE A PACKAGE</p>
                  <button type="button" className="action-btn blue" tabIndex={-1}>
                    <span className="btn-copy">VIEW RATES</span>
                    <span className="chevron">&#x203A;</span>
                  </button>
                </section>

                <section className="actions">
                  <button
                    type="button"
                    className="action-btn green"
                    onClick={handleInsertCoin}
                    aria-label="Preview insert coin"
                  >
                    <span className="btn-ico">{COIN_ICON}</span>
                    <span className="btn-copy">INSERT COIN</span>
                    <span className="chevron">&#x203A;</span>
                  </button>
                  <button
                    type="button"
                    className="action-btn amber"
                    disabled={!coinActive}
                    tabIndex={-1}
                  >
                    <span className="btn-ico">
                      <svg viewBox="0 0 24 24" aria-hidden>
                        <rect x="6" y="4.5" width="4.4" height="15" rx="0.6" fill="currentColor" />
                        <rect
                          x="13.6"
                          y="4.5"
                          width="4.4"
                          height="15"
                          rx="0.6"
                          fill="currentColor"
                        />
                      </svg>
                    </span>
                    <b>PAUSE</b>
                    <span className="chevron">&#x203A;</span>
                  </button>
                </section>

                <section className="hud-panel voucher-card" aria-hidden="true">
                  <h2>HAVE A VOUCHER?</h2>
                  <div className="voucher-row">
                    <input
                      readOnly
                      tabIndex={-1}
                      value=""
                      placeholder="ENTER VOUCHER CODE HERE..."
                    />
                    <button type="button" tabIndex={-1}>
                      CONNECT
                    </button>
                  </div>
                  <p className="voucher-help">Voucher is case-insensitive</p>
                </section>

                <footer className="footer" aria-hidden="true">
                  <div className="color-strip" />
                  <p>
                    Powered by <strong>Renz-Fi Piso WiFi</strong>
                  </p>
                  <small>Thank you for using our service!</small>
                </footer>
              </main>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
