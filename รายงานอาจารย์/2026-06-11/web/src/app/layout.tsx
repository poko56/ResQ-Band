import "./globals.css";

export const metadata = {
  title: "ระบบติดตามผู้ประสบภัย",
  description: "ระบบติดตามผู้ประสบภัยใต้ซากอาคารถล่ม",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="th">
      <body>{children}</body>
    </html>
  );
}
