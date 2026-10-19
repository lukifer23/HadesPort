import android.os.SystemClock;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.MotionEvent;
import java.lang.reflect.Method;

/** Emulator-only shell event replay. Never included in the game APK. */
public final class HadesGestureDebug {
    private static Object manager;
    private static Method inject;
    private static long down;
    private static void send(int action, float[][] points) throws Exception {
        int count=points.length;
        MotionEvent.PointerProperties[] properties=new MotionEvent.PointerProperties[count];
        MotionEvent.PointerCoords[] coordinates=new MotionEvent.PointerCoords[count];
        for(int i=0;i<count;i++) {
            properties[i]=new MotionEvent.PointerProperties();properties[i].id=i;
            properties[i].toolType=MotionEvent.TOOL_TYPE_FINGER;
            coordinates[i]=new MotionEvent.PointerCoords();coordinates[i].x=points[i][0];coordinates[i].y=points[i][1];
            coordinates[i].pressure=1;coordinates[i].size=.03f;
        }
        MotionEvent event=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,count,properties,coordinates,0,0,1,1,0,0,InputDevice.SOURCE_TOUCHSCREEN,0);
        try {if(!Boolean.TRUE.equals(inject.invoke(manager,event,2)))throw new IllegalStateException("Event injection rejected");}
        finally {event.recycle();}
    }
    public static void main(String[] args) throws Exception {
        Class<?> type=Class.forName("android.hardware.input.InputManagerGlobal");
        manager=type.getDeclaredMethod("getInstance").invoke(null);
        inject=type.getMethod("injectInputEvent",InputEvent.class,int.class);
        down=SystemClock.uptimeMillis();
        send(MotionEvent.ACTION_DOWN,new float[][]{{250,900}});
        send(MotionEvent.ACTION_MOVE,new float[][]{{320,900}});
        send(MotionEvent.ACTION_POINTER_DOWN|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new float[][]{{320,900},{800,280}});
        send(MotionEvent.ACTION_POINTER_DOWN|(2<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new float[][]{{320,900},{800,280},{1200,280}});
        for(int i=1;i<=24;i++) {
            send(MotionEvent.ACTION_MOVE,new float[][]{{320,900},{800,280},{1200+12*i,280}});
            Thread.sleep(25);
        }
        send(MotionEvent.ACTION_POINTER_UP|(2<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new float[][]{{320,900},{800,280},{1488,280}});
        send(MotionEvent.ACTION_POINTER_UP|(1<<MotionEvent.ACTION_POINTER_INDEX_SHIFT),new float[][]{{320,900},{800,280}});
        Thread.sleep(150);
        send(MotionEvent.ACTION_UP,new float[][]{{320,900}});
        System.out.println("EMULATOR_MULTITOUCH_REPLAY_OK movement_pointer=0 pinch_pointers=1,2");
    }
}
