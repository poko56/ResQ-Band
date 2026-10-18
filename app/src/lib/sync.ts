import { getFirebaseDb } from "./firebase";
import { useResQ } from "./store";

const SESSION_DOC_ID = "current";

let isRestoring = false;

/**
 * Initializes two-way sync: Zustand <-> Firestore
 * Should only be called once, strictly on the client.
 */
export async function initFirebaseSync() {
  if (typeof window === "undefined") return () => {};

  // Without the web API key (NEXT_PUBLIC_FIREBASE_API_KEY in app/.env.local)
  // every Firestore call fails, so run offline instead of erroring.
  if (!process.env.NEXT_PUBLIC_FIREBASE_API_KEY) {
    console.info("[Firebase Sync] ไม่มี NEXT_PUBLIC_FIREBASE_API_KEY - ทำงานแบบออฟไลน์ (ไม่ sync)");
    return () => {};
  }

  const db = await getFirebaseDb();
  if (!db) return () => {};

  const { doc, onSnapshot, setDoc } = await import("firebase/firestore");
  const docRef = doc(db, "sessions", SESSION_DOC_ID);

  // 1. Listen to Firestore and update Zustand
  const unsubscribe = onSnapshot(docRef, (snap) => {
    if (snap.exists()) {
      const data = snap.data();
      isRestoring = true;
      useResQ.setState({
        incident: data.incident || useResQ.getState().incident,
        wristbands: data.wristbands || {},
        anchors: data.anchors || {},
        sightings: data.sightings || {},
        timeline: data.timeline || [],
      });
      // Small delay to ensure any synchronous Zustand effects finish
      setTimeout(() => {
        isRestoring = false;
      }, 10);
    }
  });

  // 2. Listen to Zustand and update Firestore (Debounced)
  let timeout: NodeJS.Timeout | null = null;
  const unsubscribeZustand = useResQ.subscribe((state, prevState) => {
    if (isRestoring) return;

    // Only sync if actual core data changed
    if (
      state.incident !== prevState.incident ||
      state.wristbands !== prevState.wristbands ||
      state.anchors !== prevState.anchors ||
      state.sightings !== prevState.sightings ||
      state.timeline !== prevState.timeline
    ) {
      if (timeout) clearTimeout(timeout);
      timeout = setTimeout(() => {
        // Must read fresh state inside timeout
        const { incident, wristbands, anchors, sightings, timeline } = useResQ.getState();
        // Firestore rejects `undefined` field values (e.g. an unset
        // priorityScore) and throws synchronously; a JSON round-trip drops them.
        const payload = JSON.parse(JSON.stringify({ incident, wristbands, anchors, sightings, timeline }));
        try {
          setDoc(docRef, { ...payload, updatedAt: Date.now() }, { merge: true }).catch((err) => {
            console.warn("[Firebase Sync] Error saving to Firestore:", err);
          });
        } catch (err) {
          console.warn("[Firebase Sync] Invalid data for Firestore:", err);
        }
      }, 500); // 500ms debounce is responsive enough without spamming
    }
  });

  return () => {
    unsubscribe();
    unsubscribeZustand();
  };
}
