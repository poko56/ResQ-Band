// Config from firebase apps:sdkconfig
const firebaseConfig = {
  apiKey: process.env.NEXT_PUBLIC_FIREBASE_API_KEY,
  authDomain: "resq-band-app.firebaseapp.com",
  projectId: "resq-band-app",
  storageBucket: "resq-band-app.firebasestorage.app",
  messagingSenderId: "587784384994",
  appId: process.env.NEXT_PUBLIC_FIREBASE_APP_ID
};

let dbInstance: any = null;

export async function getFirebaseDb() {
  if (typeof window === 'undefined') return null;
  if (dbInstance) return dbInstance;
  
  const { initializeApp, getApps, getApp } = await import("firebase/app");
  const { getFirestore } = await import("firebase/firestore");
  
  const app = !getApps().length ? initializeApp(firebaseConfig) : getApp();
  dbInstance = getFirestore(app);
  return dbInstance;
}
