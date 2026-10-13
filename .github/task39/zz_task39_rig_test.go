package cp3b

// THROWAWAY, AND NEVER MERGED: message-windows branch ci/task39-shots on the owner's fork copies
// this file into urnetwork/message's sdk/cp3b on a GitHub runner, at T1a's head, to give the
// Windows app a message server and a second party for hardening plan Task 39's screenshots. It is
// never committed to urnetwork/message.
//
// The server is the real pipeline (peer + api + a memory store) behind message-server's own
// endpoint, on a loopback port with a self-signed key the app pins: newEndpointWorld, exactly as
// endpointroute_test.go drives it. The founder is a urmessage.Device over that endpoint, the same
// part sdk/livepeer plays: it reads the key package the app writes beside its credential, and
// writes back the invitation the app polls for. Every later key package the app publishes is
// answered too (unless TASK39_RIG_ONCE=1): the founder removes the app's old leaf and adds the new
// one, which is what an admin does for a member whose copy of the group no longer opens.

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/urnetwork/message/sdk/urmessage"
)

func task39StableRead(path string) ([]byte, bool) {
	first, err := os.ReadFile(path)
	if err != nil || len(first) == 0 {
		return nil, false
	}
	time.Sleep(300 * time.Millisecond)
	second, err := os.ReadFile(path)
	if err != nil || !bytes.Equal(first, second) {
		return nil, false
	}
	return second, true
}

func task39WriteAtomic(path string, content []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return err
	}
	temp := path + ".partial"
	if err := os.WriteFile(temp, content, 0o600); err != nil {
		return err
	}
	if err := os.Rename(temp, path); err != nil {
		os.Remove(temp)
		return err
	}
	return nil
}

func TestTask39Rig(t *testing.T) {
	rigDir := os.Getenv("TASK39_RIG_DIR")
	handshake := os.Getenv("TASK39_HANDSHAKE_DIR")
	if rigDir == "" || handshake == "" {
		t.Skip("TASK39_RIG_DIR and TASK39_HANDSHAKE_DIR are not set: this is the Task 39 runner rig")
	}
	once := os.Getenv("TASK39_RIG_ONCE") == "1"
	ctx := context.Background()

	world := newEndpointWorld(t)
	founder := world.device(t, "founder")
	if err := founder.Connect(ctx); err != nil {
		t.Fatalf("RIG the founder's Connect: %v", err)
	}
	// THE APP'S DOOR IS GATED: a second loopback port that forwards to the endpoint only while
	// rigDir/gate-open exists, and otherwise drops the connection, so a driver can hold an app's
	// connect back (it retries) while it does something to the disk between the app opening its
	// state store and its first restore. TCP is forwarded as it is: the TLS session, and the pin
	// the app checks, are the endpoint's own.
	gateFile := filepath.Join(rigDir, "gate-open")
	gated, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("RIG listening for the gated door: %v", err)
	}
	t.Cleanup(func() { gated.Close() })
	target := world.listener.Addr().String()
	go func() {
		for {
			conn, err := gated.Accept()
			if err != nil {
				return
			}
			if _, err := os.Stat(gateFile); err != nil {
				conn.Close()
				continue
			}
			go func(conn net.Conn) {
				defer conn.Close()
				upstream, err := net.Dial("tcp", target)
				if err != nil {
					return
				}
				defer upstream.Close()
				done := make(chan struct{}, 2)
				go func() { io.Copy(upstream, conn); done <- struct{}{} }()
				go func() { io.Copy(conn, upstream); done <- struct{}{} }()
				<-done
			}(conn)
		}
	}()
	appUrl := strings.Replace(world.url, target, gated.Addr().String(), 1)
	endpointFile := fmt.Sprintf("%s\n%s\n", appUrl, hex.EncodeToString(world.pin))
	if err := task39WriteAtomic(filepath.Join(rigDir, "endpoint.txt"), []byte(endpointFile)); err != nil {
		t.Fatalf("RIG writing the endpoint: %v", err)
	}
	t.Logf("RIG serving %s (the app's gated door %s), pin %s; founder connected; once=%v",
		world.url, appUrl, hex.EncodeToString(world.pin), once)

	stop := filepath.Join(rigDir, "stop")
	keyPackagePath := filepath.Join(handshake, "app.keypackage")
	invitePath := filepath.Join(handshake, "app.invite")
	var group *urmessage.Group
	served := 0
	lastReceive := time.Time{}
	for {
		if _, err := os.Stat(stop); err == nil {
			t.Logf("RIG stop file seen after %d invitation(s)", served)
			return
		}
		if group != nil && time.Since(lastReceive) > 3*time.Second {
			lastReceive = time.Now()
			if got, err := group.Receive(ctx); err != nil {
				t.Logf("RIG the founder's receive: %v", err)
			} else if 0 < len(got) {
				t.Logf("RIG the founder received %d record(s)", len(got))
			}
		}
		keyPackage, ok := task39StableRead(keyPackagePath)
		if !ok || (group != nil && once) {
			time.Sleep(time.Second)
			continue
		}
		if err := os.Remove(keyPackagePath); err != nil {
			t.Logf("RIG the key package file could not be removed: %v", err)
		}
		var invite *urmessage.Invite
		var err error
		if group == nil {
			groupId := make([]byte, urmessage.GroupIdBytes)
			if _, err := rand.Read(groupId); err != nil {
				t.Fatalf("RIG group id: %v", err)
			}
			if group, err = founder.CreateGroup(ctx, groupId); err != nil {
				t.Fatalf("RIG CreateGroup: %v", err)
			}
			if invite, err = group.AddMember(keyPackage); err != nil {
				t.Fatalf("RIG AddMember: %v", err)
			}
			if err := group.Open(ctx); err != nil {
				t.Fatalf("RIG Open: %v", err)
			}
			if err := task39WriteAtomic(filepath.Join(rigDir, "gid.txt"), []byte(hex.EncodeToString(groupId))); err != nil {
				t.Fatalf("RIG writing the group id: %v", err)
			}
		} else {
			members, err := group.Members()
			if err != nil {
				t.Fatalf("RIG Members: %v", err)
			}
			removed := map[string]bool{}
			for _, member := range members {
				if member.Mine || removed[string(member.IdentityPub)] {
					continue
				}
				removed[string(member.IdentityPub)] = true
				if err := group.RemoveMember(ctx, member.IdentityPub); err != nil {
					t.Fatalf("RIG RemoveMember %x: %v", member.IdentityPub[:4], err)
				}
				t.Logf("RIG removed the app's old leaf (identity %x...) at epoch %d", member.IdentityPub[:4], group.Epoch())
			}
			if invite, err = group.AddMemberAndPublish(ctx, keyPackage); err != nil {
				t.Fatalf("RIG AddMemberAndPublish: %v", err)
			}
		}
		encoded, err := invite.Encode()
		if err != nil {
			t.Fatalf("RIG encode: %v", err)
		}
		served += 1
		if err := task39WriteAtomic(filepath.Join(rigDir, fmt.Sprintf("invite%d.bin", served)), encoded); err != nil {
			t.Fatalf("RIG keeping invitation %d: %v", served, err)
		}
		if err := task39WriteAtomic(invitePath, encoded); err != nil {
			t.Fatalf("RIG writing invitation %d: %v", served, err)
		}
		t.Logf("RIG invitation %d (%d octets) into group %x at epoch %d written to %s",
			served, len(encoded), group.Id(), group.Epoch(), invitePath)
		line := fmt.Sprintf("From the runner's founder: invitation %d is out, at epoch %d.", served, group.Epoch())
		if _, err := group.Send(ctx, line); err != nil {
			t.Logf("RIG send: %v", err)
		}
		if err := task39WriteAtomic(filepath.Join(rigDir, fmt.Sprintf("served%d.txt", served)), []byte(line)); err != nil {
			t.Fatalf("RIG marking invitation %d: %v", served, err)
		}
	}
}
