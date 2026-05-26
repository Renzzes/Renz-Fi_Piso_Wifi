import { useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { Upload } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Textarea } from "@/components/ui/textarea";

export const Route = createFileRoute("/_layout/captive-portal")({
  component: PortalPage,
});

function PortalPage() {
  const [welcome, setWelcome] = useState("Welcome to Renz-Fi");
  const [announce, setAnnounce] = useState("Insert coin to get internet access.");
  const [bg, setBg] = useState("#0f172a");
  const [accent, setAccent] = useState("#3b82f6");

  return (
    <div>
      <PageHeader title="Captive Portal" description="Customize the landing page shown to users" />
      <div className="grid md:grid-cols-2 gap-3">
        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="space-y-1">
            <Label className="text-xs">Logo</Label>
            <Input type="file" accept="image/*" className="h-9" />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Welcome Message</Label>
            <Input value={welcome} onChange={(e) => setWelcome(e.target.value)} />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Announcement Banner</Label>
            <Textarea value={announce} onChange={(e) => setAnnounce(e.target.value)} rows={3} />
          </div>
          <div className="grid grid-cols-2 gap-3">
            <div className="space-y-1">
              <Label className="text-xs">Background</Label>
              <Input type="color" value={bg} onChange={(e) => setBg(e.target.value)} className="h-9 p-1" />
            </div>
            <div className="space-y-1">
              <Label className="text-xs">Accent Color</Label>
              <Input type="color" value={accent} onChange={(e) => setAccent(e.target.value)} className="h-9 p-1" />
            </div>
          </div>
          <Button size="sm">Save Settings</Button>
        </div>

        <div className="rounded-md border bg-card p-3">
          <div className="text-xs text-muted-foreground mb-2">Preview</div>
          <div
            className="rounded-md p-6 text-center min-h-[260px] flex flex-col justify-center"
            style={{ background: bg, color: "#fff" }}
          >
            <div
              className="mx-auto h-12 w-12 rounded-full mb-3"
              style={{ background: accent }}
            />
            <h3 className="text-lg font-semibold">{welcome}</h3>
            <p className="text-xs opacity-80 mt-2">{announce}</p>
            <button
              className="mt-4 mx-auto px-4 py-1.5 rounded-md text-sm font-medium"
              style={{ background: accent, color: "#fff" }}
            >
              Connect
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
