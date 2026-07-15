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
        setDoc(docRef, {
          incident,
          wristbands,
          anchors,
          sightings,
          timeline,
          updatedAt: Date.now()
        }, { merge: true }).catch((err) => {
          console.warn("[Firebase Sync] Error saving to Firestore:", err);
        });
      }, 500); // 500ms debounce is responsive enough without spamming
    }
  });

  return () => {
    unsubscribe();
    unsubscribeZustand();
  };
}
