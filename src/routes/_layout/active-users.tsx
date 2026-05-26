import { useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { Wifi, WifiOff } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { Badge } from "@/components/ui/badge";

export const Route = createFileRoute("/_layout/active-users")({
  component: ActiveUsersPage,
});

type User = { mac: string; ip: string; remaining: string; device: string };

const seed: User[] = [
  { mac: "A4:C3:F0:12:8B:9D", ip: "10.10.10.21", remaining: "1h 22m", device: "iPhone" },
  { mac: "B8:27:EB:55:AA:01", ip: "10.10.10.34", remaining: "0h 14m", device: "Android" },
  { mac: "DC:A6:32:9F:11:42", ip: "10.10.10.45", remaining: "3h 05m", device: "Laptop" },
  { mac: "E4:5F:01:7C:88:33", ip: "10.10.10.58", remaining: "0h 02m", device: "Android" },
];

function ActiveUsersPage() {
  const [users, setUsers] = useState(seed);
  const kick = (mac: string) => setUsers((u) => u.filter((x) => x.mac !== mac));

  return (
    <div>
      <PageHeader title="Active Users" description={`${users.length} devices connected`} />
      <div className="rounded-md border bg-card overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>Device</TableHead>
              <TableHead>MAC</TableHead>
              <TableHead>IP</TableHead>
              <TableHead>Remaining</TableHead>
              <TableHead className="text-right">Action</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {users.map((u) => (
              <TableRow key={u.mac}>
                <TableCell className="flex items-center gap-2">
                  <Wifi className="h-3.5 w-3.5 text-emerald-500" /> {u.device}
                </TableCell>
                <TableCell className="font-mono text-xs">{u.mac}</TableCell>
                <TableCell className="font-mono text-xs">{u.ip}</TableCell>
                <TableCell>
                  <Badge variant="secondary">{u.remaining}</Badge>
                </TableCell>
                <TableCell className="text-right">
                  <Button size="sm" variant="destructive" className="h-7" onClick={() => kick(u.mac)}>
                    <WifiOff className="h-3.5 w-3.5" /> Disconnect
                  </Button>
                </TableCell>
              </TableRow>
            ))}
            {users.length === 0 && (
              <TableRow>
                <TableCell colSpan={5} className="text-center text-muted-foreground py-6 text-sm">
                  No active users
                </TableCell>
              </TableRow>
            )}
          </TableBody>
        </Table>
      </div>
    </div>
  );
}
