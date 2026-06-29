import React, { useState, useEffect, useCallback } from 'react';
import { BrowserRouter as Router, Routes, Route, useNavigate } from 'react-router-dom';
import {
  Bot, Upload, Settings, User, Activity, Shield, ArrowRight, FileText,
  ChevronLeft, Volume2, Phone, AlertTriangle, Heart, Users, CheckCircle,
  Clock, Plus, Trash2, AlertOctagon, Fingerprint, Bell, RefreshCw, Wifi, WifiOff
} from 'lucide-react';
import './index.css';

// ─────────────────────────────────────────────────────────────────────────────
// כתובת ה-ESP32 — שנה כאן אם ה-IP השתנה
// ─────────────────────────────────────────────────────────────────────────────
const ESP32_BASE = 'http://192.168.1.100';

// ─────────────────────────────────────────────────────────────────────────────
// hook מרכזי: קריאת סטטוס הקופסה בזמן אמת (polling כל 4 שניות)
// מחזיר: { status, isAway, volume, waitingFingerprint, pendingCompartment, online }
// ─────────────────────────────────────────────────────────────────────────────
function useBoxStatus(enabled = true) {
  const [boxData, setBoxData] = useState({
    status: 'Offline',
    isAway: false,
    volume: 80,
    waitingFingerprint: false,
    pendingCompartment: 0,
    online: false,
  });

  useEffect(() => {
    if (!enabled) return;
    let isMounted = true;
    let timeoutId = null;

    const poll = async () => {
      const ctrl = new AbortController();
      const timer = setTimeout(() => ctrl.abort(), 3000);
      try {
        const res = await fetch(`${ESP32_BASE}/api/box-status`, {
          signal: ctrl.signal,
          headers: { 'Cache-Control': 'no-cache' },
        });
        clearTimeout(timer);
        if (res.ok && isMounted) {
          const data = await res.json();
          setBoxData({ ...data, online: true });
        }
      } catch {
        if (isMounted) setBoxData(prev => ({ ...prev, online: false, waitingFingerprint: false }));
      } finally {
        if (isMounted) timeoutId = setTimeout(poll, 4000);
      }
    };

    poll();
    return () => { isMounted = false; clearTimeout(timeoutId); };
  }, [enabled]);

  return boxData;
}

// ─────────────────────────────────────────────────────────────────────────────
// רכיב: כרזת "ממתין לטביעת אצבע" — מוצג בכל עמוד שמקבל boxData
// כשהקופסה ב-STATE_WAITING_FINGERPRINT, בן המשפחה יכול ללחוץ "אשר עבורי"
// ─────────────────────────────────────────────────────────────────────────────
const FingerprintWaitingBanner = ({ boxData }) => {
  const [approving, setApproving] = useState(false);
  const [approveResult, setApproveResult] = useState(null); // 'ok' | 'error'

  if (!boxData.waitingFingerprint) return null;

  const handleRemoteApprove = async () => {
    setApproving(true);
    setApproveResult(null);
    try {
      const res = await fetch(`${ESP32_BASE}/api/remote-approve`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ approvedBy: 'family_member' }),
      });
      setApproveResult(res.ok ? 'ok' : 'error');
    } catch {
      setApproveResult('error');
    } finally {
      setApproving(false);
    }
  };

  return (
    <div className="safety-alert-pulse" style={{
      background: '#FFF7ED', borderRadius: '16px', padding: '1.5rem',
      marginBottom: '2rem', display: 'flex', gap: '1rem', alignItems: 'center',
      flexWrap: 'wrap'
    }}>
      <div style={{ background: '#FED7AA', padding: '10px', borderRadius: '50%', flexShrink: 0 }}>
        <Fingerprint color="#C2410C" size={28} />
      </div>
      <div style={{ flex: 1 }}>
        <h3 style={{ color: '#9A3412', margin: '0 0 0.25rem 0', fontSize: '1.1rem' }}>
          הקופסה ממתינה לאימות טביעת אצבע
        </h3>
        <p style={{ color: '#7C2D12', margin: 0, fontSize: '0.9rem' }}>
          תא מספר <strong>{boxData.pendingCompartment}</strong> ממתין לפתיחה.
          אם המשתמש אינו יכול לאמת, בן משפחה מורשה יכול לאשר כאן.
        </p>
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: '0.5rem', alignItems: 'flex-end' }}>
        {approveResult === 'ok' ? (
          <span style={{ color: '#16A34A', fontWeight: '600', display: 'flex', alignItems: 'center', gap: '6px' }}>
            <CheckCircle size={18} /> אושר! הקופסה פותחת את התא
          </span>
        ) : approveResult === 'error' ? (
          <span style={{ color: '#DC2626', fontSize: '0.85rem' }}>שגיאה — נסה שוב</span>
        ) : null}
        {approveResult !== 'ok' && (
          <button
            className="btn-primary"
            onClick={handleRemoteApprove}
            disabled={approving}
            style={{ background: 'linear-gradient(135deg, #C2410C 0%, #EA580C 100%)', opacity: approving ? 0.7 : 1 }}
          >
            {approving ? <RefreshCw size={16} style={{ animation: 'spin 1s linear infinite' }} /> : <Fingerprint size={16} />}
            {approving ? 'שולח אישור...' : 'אשר עבורי'}
          </button>
        )}
      </div>
    </div>
  );
};

// ─────────────────────────────────────────────────────────────────────────────
// Header
// ─────────────────────────────────────────────────────────────────────────────
const Header = ({ boxOnline }) => {
  const navigate = useNavigate();
  return (
    <header className="app-header" style={{ padding: '1rem 2rem', display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
      <div
        onClick={() => navigate('/')}
        style={{ display: 'flex', alignItems: 'center', gap: '12px', cursor: 'pointer' }}
        title="חזרה לדף הבית"
      >
        <div style={{ background: '#E3F2FD', padding: '8px', borderRadius: '10px' }}>
          <Bot size={28} color="var(--primary)" />
        </div>
        <h1 style={{ fontSize: '1.5rem', margin: 0, fontWeight: '700', letterSpacing: '-0.5px' }}>
          <span style={{ color: 'var(--primary)' }}>Medi</span>
          <span style={{ color: 'var(--accent)' }}>Mate</span>
        </h1>
      </div>
      <div style={{ display: 'flex', alignItems: 'center', gap: '1.5rem' }}>
        {/* אינדיקטור חיבור לקופסה */}
        <div style={{ display: 'flex', alignItems: 'center', gap: '6px', fontSize: '0.8rem', color: boxOnline ? '#16A34A' : '#94a3b8' }}>
          {boxOnline ? <Wifi size={16} /> : <WifiOff size={16} />}
          {boxOnline ? 'קופסה מחוברת' : 'קופסה לא מחוברת'}
        </div>
        <button
          onClick={() => navigate('/personal')}
          style={{ background: 'none', border: '1px solid #e2e8f0', borderRadius: '10px', padding: '8px 14px', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '6px', color: '#374151', fontSize: '0.9rem', fontWeight: '500' }}
        >
          <User size={16} /> אזור אישי
        </button>
      </div>
    </header>
  );
};

// ─────────────────────────────────────────────────────────────────────────────
// LandingPage — ללא שינוי מהותי
// ─────────────────────────────────────────────────────────────────────────────
const LandingPage = () => {
  const navigate = useNavigate();
  return (
    <div style={{ maxWidth: '1200px', margin: '0 auto', padding: '4rem 1.5rem', textAlign: 'center' }}>
      <div style={{ marginBottom: '5rem' }}>
        <span style={{ background: '#E3F2FD', color: 'var(--primary)', padding: '6px 16px', borderRadius: '20px', fontSize: '0.9rem', fontWeight: '600' }}>פרויקט גמר חדשני</span>
        <h1 style={{ fontSize: '3.5rem', margin: '1.5rem 0', lineHeight: 1.1, color: '#0f172a' }}>
          החבר הבריאותי שלך<br /><span style={{ color: 'var(--primary)' }}>לשקט נפשי אמיתי</span>
        </h1>
        <p style={{ fontSize: '1.25rem', color: '#64748b', maxWidth: '700px', margin: '0 auto 2rem auto' }}>
          מערכת חכמה המשלבת רובוטיקה ובינה מלאכותית כדי למנוע טעויות בנטילת תרופות ולהציל חיים.
        </p>
        <button className="btn-primary" onClick={() => navigate('/login')} style={{ fontSize: '1.1rem', padding: '16px 40px' }}>
          כניסה למערכת <ArrowRight size={20} />
        </button>
      </div>

      <div style={{ marginBottom: '5rem' }}>
        <h2 style={{ fontSize: '2rem', marginBottom: '2rem', color: '#1e293b' }}>למה אנחנו כאן? <span style={{ color: 'var(--accent)' }}>הנתונים מדברים בעד עצמם</span></h2>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(250px, 1fr))', gap: '2rem', textAlign: 'right' }}>
          {[
            { icon: <Activity size={24} color="var(--accent)" />, bg: '#FEE2E2', val: '38%', color: 'var(--accent)', title: 'מהקשישים בישראל', desc: 'נוטלים מעל 5 תרופות שונות ביום, מה שמגדיל דרמטית את הסיכון לטעויות.' },
            { icon: <AlertTriangle size={24} color="#D97706" />, bg: '#FEF3C7', val: '27%', color: '#D97706', title: 'טעויות תחת השגחה', desc: 'מהדיירים בבתי אבות חווים טעויות במתן תרופות, למרות הצוות המקצועי.' },
            { icon: <Heart size={24} color="#64748b" />, bg: '#F1F5F9', val: '125,000', color: '#64748b', title: 'מקרי מוות בשנה', desc: 'בארה"ב בלבד, כתוצאה ישירה מאי-הקפדה על נטילת תרופות נכונה.' },
            { icon: <Users size={24} color="var(--primary)" />, bg: '#E0F2FE', val: '57%', color: 'var(--primary)', title: 'קושי של בני המשפחה', desc: 'מהמטפלים העיקריים מדווחים על קושי ונטל נפשי בניהול התרופות של יקיריהם.' },
          ].map((s, i) => (
            <div key={i} className="card">
              <div className="btn-icon-wrapper" style={{ background: s.bg }}>{s.icon}</div>
              <h3 style={{ fontSize: '2rem', margin: '0 0 0.5rem 0', color: s.color }}>{s.val}</h3>
              <p style={{ color: '#64748b', fontWeight: '500' }}>{s.title}</p>
              <p style={{ fontSize: '0.9rem', color: '#94a3b8' }}>{s.desc}</p>
            </div>
          ))}
        </div>
      </div>

      <div style={{ background: 'var(--primary)', borderRadius: '24px', padding: '4rem 2rem', color: 'white' }}>
        <h2 style={{ fontSize: '2rem', marginBottom: '3rem' }}>המהפכה של MediMate</h2>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '3rem' }}>
          {[
            { icon: <CheckCircle size={40} color="#4ADE80" />, val: '99%', label: 'שיפור בהיענות לטיפול' },
            { icon: <Shield size={40} color="#60A5FA" />, val: '0', label: 'טעויות קריטיות' },
            { icon: <Clock size={40} color="#F472B6" />, val: '24/7', label: 'שקט נפשי למשפחה' },
          ].map((s, i) => (
            <div key={i} style={{ textAlign: 'center' }}>
              <div style={{ background: 'rgba(255,255,255,0.1)', width: '80px', height: '80px', borderRadius: '50%', display: 'flex', alignItems: 'center', justifyContent: 'center', margin: '0 auto 1.5rem auto' }}>{s.icon}</div>
              <h3 style={{ fontSize: '2.5rem', margin: 0, fontWeight: '700' }}>{s.val}</h3>
              <p style={{ fontSize: '1.2rem', opacity: 0.9 }}>{s.label}</p>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
};

// ─────────────────────────────────────────────────────────────────────────────
// LoginPage
// ─────────────────────────────────────────────────────────────────────────────
const LoginPage = () => {
  const navigate = useNavigate();
  return (
    <div style={{ minHeight: '80vh', display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '1rem' }}>
      <div className="card" style={{ width: '100%', maxWidth: '420px', padding: '3rem' }}>
        <div style={{ textAlign: 'center', marginBottom: '2rem' }}>
          <h2 style={{ fontSize: '1.8rem', marginBottom: '0.5rem' }}>ברוכים השבים</h2>
          <p style={{ color: '#64748b' }}>הכנס את פרטי המשתמש שלך</p>
        </div>
        {/* תיקון: הסרנו onSubmit עם form ועברנו לכפתור רגיל */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem' }}>
          <div>
            <label style={{ display: 'block', marginBottom: '8px', fontWeight: '500', fontSize: '0.9rem' }}>אימייל</label>
            <input type="email" placeholder="name@example.com" />
          </div>
          <div>
            <label style={{ display: 'block', marginBottom: '8px', fontWeight: '500', fontSize: '0.9rem' }}>סיסמה</label>
            <input type="password" placeholder="••••••••" />
          </div>
          <button className="btn-primary" style={{ marginTop: '1rem' }} onClick={() => navigate('/dashboard')}>
            כניסה למערכת
          </button>
        </div>
      </div>
    </div>
  );
};

// ─────────────────────────────────────────────────────────────────────────────
// DashboardPage
// ─────────────────────────────────────────────────────────────────────────────
const DashboardPage = () => {
  const navigate = useNavigate();
  const boxData = useBoxStatus(true);

  const medicationHistory = [
    { id: 1, name: 'אספירין', time: '08:00', status: 'ok', msg: 'נלקח בזמן' },
    { id: 2, name: 'ויטמין D', time: '08:00', status: 'ok', msg: 'נלקח בזמן' },
    { id: 3, name: 'לחץ דם', time: '12:00', status: 'late', msg: 'נלקח באיחור של שעתיים' },
    { id: 4, name: 'אומפרדקס', time: '14:00', status: 'danger', msg: 'חריגת יתר! לא נלקח' },
  ];

  const hasCriticalWarning = medicationHistory.some(m => m.status === 'danger');

  // מיפוי מצב מכונת המצבים לתצוגה בעברית
  const statusLabel = {
    IDLE: '✅ ממתין — הכל תקין',
    ALARM_RINGING: '🔔 זמן מנה! נא להתקרב',
    WAITING_FINGERPRINT: '👆 ממתין לטביעת אצבע',
    ROBOT_DEPLOYED: '🤖 רובוט בחיפוש',
    DISPENSING: '💊 משחרר תרופה...',
    CALLING_TWILIO: '📞 מתקשר לחירום',
    ERROR: '❌ שגיאה',
    Offline: '📵 לא מחובר',
  };

  return (
    <div style={{ maxWidth: '1000px', margin: '0 auto', padding: '3rem 1.5rem' }}>

      {/* ── כרזת אישור מרחוק ── */}
      <FingerprintWaitingBanner boxData={boxData} />

      {/* ── אזהרת בטיחות ── */}
      {hasCriticalWarning && (
        <div style={{ background: '#FEF2F2', border: '1px solid #FECACA', borderRadius: '16px', padding: '1.5rem', marginBottom: '2rem', display: 'flex', gap: '1rem', alignItems: 'start' }}>
          <div style={{ background: '#FCA5A5', padding: '10px', borderRadius: '50%' }}>
            <AlertOctagon color="#7F1D1D" size={24} />
          </div>
          <div>
            <h3 style={{ color: '#991B1B', margin: '0 0 0.5rem 0' }}>התראת בטיחות חמורה</h3>
            <p style={{ color: '#7F1D1D', margin: 0 }}>זוהתה חריגה בנטילת התרופות היום (אומפרדקס). המערכת שלחה התראה לאנשי הקשר לחירום.</p>
          </div>
        </div>
      )}

      {/* ── כרטיס מצב קופסה ── */}
      <div className="card" style={{ marginBottom: '2rem', display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: '1rem' }}>
        <div>
          <p style={{ margin: 0, fontSize: '0.85rem', color: '#64748b' }}>מצב הקופסה כעת</p>
          <p style={{ margin: 0, fontSize: '1.15rem', fontWeight: '600', color: '#1e293b' }}>
            {statusLabel[boxData.status] || boxData.status}
          </p>
        </div>
        <div style={{ display: 'flex', gap: '2rem', flexWrap: 'wrap' }}>
          <div style={{ textAlign: 'center' }}>
            <p style={{ margin: 0, fontSize: '0.8rem', color: '#94a3b8' }}>עוצמת שמע</p>
            <p style={{ margin: 0, fontWeight: '700', color: 'var(--primary)' }}>{boxData.volume}%</p>
          </div>
          <div style={{ textAlign: 'center' }}>
            <p style={{ margin: 0, fontSize: '0.8rem', color: '#94a3b8' }}>מצב חופשה</p>
            <p style={{ margin: 0, fontWeight: '700', color: boxData.isAway ? '#D97706' : '#16A34A' }}>{boxData.isAway ? 'פעיל' : 'כבוי'}</p>
          </div>
        </div>
      </div>

      <div style={{ marginBottom: '3rem' }}>
        <h2 style={{ fontSize: '2rem', margin: 0 }}>שלום, רחל! 👋</h2>
        <p style={{ color: '#64748b', marginTop: '0.5rem' }}>הנה תמונת המצב שלך להיום</p>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(350px, 1fr))', gap: '2rem', marginBottom: '3rem' }}>
        {[
          { path: '/upload', icon: <Upload size={24} color="var(--primary)" />, bg: '#E3F2FD', accent: 'var(--primary)', title: 'העלאת מרשמים', desc: 'סרוק מרשם חדש והמערכת תעדכן את הקופסה אוטומטית.' },
          { path: '/settings', icon: <Settings size={24} color="var(--accent)" />, bg: '#FEE2E2', accent: 'var(--accent)', title: 'הגדרות וחירום', desc: 'ניהול אנשי קשר, התראות ומצב חופשה.' },
        ].map((card, i) => (
          <div key={i} className="card hover-card" onClick={() => navigate(card.path)} style={{ cursor: 'pointer', position: 'relative', overflow: 'hidden' }}>
            <div style={{ position: 'absolute', top: 0, right: 0, width: '6px', height: '100%', background: card.accent }} />
            <div style={{ display: 'flex', justifyContent: 'space-between' }}>
              <div>
                <div className="btn-icon-wrapper" style={{ background: card.bg }}>{card.icon}</div>
                <h3 style={{ fontSize: '1.4rem', marginBottom: '0.5rem' }}>{card.title}</h3>
                <p style={{ color: '#64748b' }}>{card.desc}</p>
              </div>
              <ArrowRight color="#cbd5e1" />
            </div>
          </div>
        ))}
      </div>

      <div className="card">
        <h3 style={{ fontSize: '1.5rem', marginBottom: '1.5rem', display: 'flex', alignItems: 'center', gap: '10px' }}>
          <Clock size={24} color="#64748b" /> היסטוריית נטילה יומית
        </h3>
        <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
          {medicationHistory.map((item) => (
            <div key={item.id} style={{
              display: 'flex', alignItems: 'center', justifyContent: 'space-between',
              padding: '1rem', borderRadius: '12px',
              backgroundColor: item.status === 'danger' ? '#FEF2F2' : item.status === 'late' ? '#FFFBEB' : '#F0FDF4',
              border: item.status === 'danger' ? '1px solid #FECACA' : '1px solid transparent'
            }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '15px' }}>
                {item.status === 'ok' ? <CheckCircle color="#16A34A" /> : item.status === 'late' ? <Clock color="#D97706" /> : <AlertTriangle color="#DC2626" />}
                <div>
                  <div style={{ fontWeight: 'bold' }}>{item.name}</div>
                  <div style={{ fontSize: '0.9rem', color: '#64748b' }}>זמן מיועד: {item.time}</div>
                </div>
              </div>
              <div style={{ fontWeight: '500', color: item.status === 'danger' ? '#DC2626' : item.status === 'late' ? '#D97706' : '#16A34A' }}>
                {item.msg}
              </div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
};

// ─────────────────────────────────────────────────────────────────────────────
// UploadPage — תוקן: handleUpload היה שם פונקציה שגוי (לא הוגדר),
//             השם הנכון הוא handleUploadToCloud
// ─────────────────────────────────────────────────────────────────────────────
const UploadPage = () => {
  const navigate = useNavigate();
  const [loading, setLoading] = useState(false);
  const [validationState, setValidationState] = useState(null);
  const [parsedData, setParsedData] = useState(null);
  const [selectedFile, setSelectedFile] = useState(null);

  const handleFileSelect = (file) => {
    if (file && (file.type.startsWith('image/') || file.type === 'application/pdf')) {
      setSelectedFile(file);
      setValidationState(null);
      setParsedData(null);
    } else {
      alert('אנא בחר קובץ תמונה או PDF בלבד');
    }
  };

  // ─── שליחת המרשם לשרת הענן לניתוח OCR ───────────────────────────────────
  // (לא ל-ESP32! ה-ESP32 לא מסוגל להריץ OCR)
  const handleUploadToCloud = async () => {
    if (!selectedFile) return;
    setLoading(true);
    setValidationState(null);

    const formData = new FormData();
    formData.append('prescriptionImage', selectedFile);

    try {
      const response = await fetch('https://your-cloud-api.com/api/analyze', {
        method: 'POST',
        body: formData,
      });

      if (!response.ok) throw new Error(`Server error: ${response.status}`);

      const data = await response.json();
      setParsedData(data.medicationDetails);
      setValidationState(data.safetyStatus);

    } catch (error) {
      console.error('Upload failed:', error);
      setValidationState('invalid');
    } finally {
      setLoading(false);
    }
  };

  // ─── שמירה ל-ESP32 לאחר אישור המשתמש ────────────────────────────────────
  const confirmAndSave = async () => {
    try {
      const response = await fetch(`${ESP32_BASE}/api/save-prescription`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(parsedData),
      });

      if (response.ok) {
        alert('הנתונים אומתו ונשלחו לקופסת התרופות בבטחה.');
        navigate('/dashboard');
      } else {
        throw new Error('Save failed');
      }
    } catch (error) {
      console.error('Error saving prescription:', error);
      alert('שגיאה בשמירת הנתונים. אנא נסה שוב.');
    }
  };

  return (
    <div style={{ maxWidth: '600px', margin: '2rem auto', padding: '1.5rem' }}>
      <button onClick={() => navigate('/dashboard')} style={{ background: 'none', border: 'none', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '2rem', color: '#64748b', fontWeight: '500' }}>
        <ChevronLeft size={20} /> חזרה לראשי
      </button>

      <div className="card" style={{ textAlign: 'center', padding: '4rem 2rem' }}>
        <div style={{ background: '#F1F5F9', width: '100px', height: '100px', borderRadius: '50%', display: 'flex', alignItems: 'center', justifyContent: 'center', margin: '0 auto 2rem auto' }}>
          <FileText size={48} color="var(--primary)" />
        </div>
        <h2 style={{ fontSize: '1.8rem', marginBottom: '1rem' }}>סריקת מרשם</h2>

        {!validationState && (
          <>
            <p style={{ color: '#64748b', marginBottom: '2rem' }}>צלם את המרשם או גרור קובץ לכאן</p>
            <div style={{ marginBottom: '2rem' }}>
              <input id="file-input" type="file" accept="image/*,.pdf" onChange={e => handleFileSelect(e.target.files[0])} style={{ display: 'none' }} />
              <button
                onClick={() => document.getElementById('file-input').click()}
                style={{ background: 'none', border: '2px dashed #cbd5e1', padding: '2rem', borderRadius: '8px', cursor: 'pointer', width: '100%', color: '#64748b' }}
              >
                {selectedFile ? selectedFile.name : 'בחר קובץ...'}
              </button>
            </div>
            {/* תיקון: handleUpload → handleUploadToCloud */}
            <button className="btn-primary" onClick={handleUploadToCloud} disabled={loading || !selectedFile} style={{ width: '100%', opacity: (loading || !selectedFile) ? 0.7 : 1 }}>
              {loading ? 'מנתח נתונים בשרת...' : 'העלה ונתח מרשם'}
            </button>
          </>
        )}

        {validationState === 'invalid' && (
          <div style={{ background: '#FEE2E2', padding: '1rem', borderRadius: '8px', color: '#991B1B', marginTop: '1rem' }}>
            <AlertTriangle size={24} style={{ marginBottom: '10px' }} />
            <p><strong>העלאת מסמך שאינו מרשם רפואי חסומה מטעמי בטיחות.</strong></p>
            <button onClick={() => setValidationState(null)} className="btn-primary" style={{ marginTop: '1rem' }}>נסה שוב</button>
          </div>
        )}

        {validationState === 'unknown' && (
          <div style={{ background: '#FEF3C7', padding: '1rem', borderRadius: '8px', color: '#92400E', marginTop: '1rem' }}>
            <AlertTriangle size={24} style={{ marginBottom: '10px' }} />
            <p><strong>לא הצלחנו לאמת את שם התרופה במאגר הרשמי. אנא הקלד את השם ידנית.</strong></p>
            <button onClick={() => setValidationState(null)} className="btn-primary" style={{ marginTop: '1rem' }}>הזנה ידנית / נסה שוב</button>
          </div>
        )}

        {validationState === 'warning' && (
          <div style={{ background: '#FEF3C7', padding: '1rem', borderRadius: '8px', color: '#92400E', marginTop: '1rem', border: '2px solid #D97706' }}>
            <AlertOctagon size={24} style={{ marginBottom: '10px' }} />
            <p><strong>שים לב: זיהינו נטילות קרובות מדי (פחות משעתיים).</strong></p>
            <p>תרופה: {parsedData?.name} | שעות: {parsedData?.times?.join(', ')}</p>
            <button onClick={confirmAndSave} className="btn-primary" style={{ marginTop: '1rem', background: '#D97706' }}>הבנתי, שמור בכל זאת</button>
          </div>
        )}

        {validationState === 'success' && (
          <div style={{ background: '#F0FDF4', padding: '1rem', borderRadius: '8px', color: '#166534', marginTop: '1rem' }}>
            <CheckCircle size={24} style={{ marginBottom: '10px' }} />
            <p><strong>המרשם פוענח בהצלחה.</strong></p>
            <p>תרופה: {parsedData?.name} | שעות: {parsedData?.times?.join(', ')}</p>
            <button onClick={confirmAndSave} className="btn-primary" style={{ marginTop: '1rem', background: '#16A34A' }}>אשר ושמור לקופסה</button>
          </div>
        )}
      </div>
    </div>
  );
};

// ─────────────────────────────────────────────────────────────────────────────
// SettingsPage — תוקנו ה-endpoints: GET+PUT על /api/box-status
// ─────────────────────────────────────────────────────────────────────────────
const SettingsPage = () => {
  const navigate = useNavigate();
  const [isAway, setIsAway] = useState(false);
  const [volume, setVolume] = useState(80);
  const [saving, setSaving] = useState(false);
  const [contacts, setContacts] = useState([
    { id: 1, name: 'בן (הבן שלי)', phone: '050-1234567' },
  ]);

  // ── טעינת מצב ראשוני מהקופסה ──────────────────────────────────────────
  useEffect(() => {
    const load = async () => {
      try {
        const res = await fetch(`${ESP32_BASE}/api/box-status`);
        if (res.ok) {
          const data = await res.json();
          if (data.isAway !== undefined) setIsAway(data.isAway);
          if (data.volume !== undefined) setVolume(data.volume);
        }
      } catch (e) {
        console.warn('לא ניתן להתחבר לקופסה לטעינת הגדרות:', e);
      }
    };
    load();
  }, []);

  // ── שמירת הגדרות ל-ESP32 ─────────────────────────────────────────────────
  const saveSettings = async () => {
    setSaving(true);
    try {
      const res = await fetch(`${ESP32_BASE}/api/box-status`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        // שליחת isAway ו-volume יחד בגוף אחד
        body: JSON.stringify({ isAway, volume }),
      });
      if (res.ok) {
        alert('ההגדרות נשמרו בקופסה בהצלחה!');
        navigate('/dashboard');
      } else {
        throw new Error(`HTTP ${res.status}`);
      }
    } catch (err) {
      alert('שגיאת תקשורת עם הקופסה — ודא שהנך מחובר לאותה רשת WiFi.');
      console.error(err);
    } finally {
      setSaving(false);
    }
  };

  const addContact = () => setContacts(prev => [...prev, { id: Date.now(), name: '', phone: '' }]);
  const removeContact = (id) => setContacts(prev => prev.filter(c => c.id !== id));
  const updateContact = (id, field, val) => setContacts(prev => prev.map(c => c.id === id ? { ...c, [field]: val } : c));

  return (
    <div style={{ maxWidth: '600px', margin: '2rem auto', padding: '1.5rem' }}>
      <button onClick={() => navigate('/dashboard')} style={{ background: 'none', border: 'none', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '2rem', color: '#64748b', fontWeight: '500' }}>
        <ChevronLeft size={20} /> חזרה לראשי
      </button>

      <div className="card">
        <h2 style={{ fontSize: '1.8rem', marginBottom: '2.5rem', borderBottom: '1px solid #f1f5f9', paddingBottom: '1rem' }}>הגדרות</h2>

        {/* אנשי קשר */}
        <div style={{ marginBottom: '2rem' }}>
          <div style={{ display: 'flex', gap: '15px', marginBottom: '1rem' }}>
            <div className="btn-icon-wrapper" style={{ background: '#FEE2E2', width: '40px', height: '40px', marginBottom: 0 }}>
              <Phone size={20} color="var(--accent)" />
            </div>
            <h4 style={{ margin: 0, fontSize: '1.1rem', lineHeight: '40px' }}>אנשי קשר לחירום</h4>
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '15px' }}>
            {contacts.map((c) => (
              <div key={c.id} style={{ display: 'flex', gap: '10px' }}>
                <input type="text" placeholder="שם מלא" value={c.name} onChange={e => updateContact(c.id, 'name', e.target.value)} />
                <input type="text" placeholder="טלפון" value={c.phone} onChange={e => updateContact(c.id, 'phone', e.target.value)} />
                {contacts.length > 1 && (
                  <button onClick={() => removeContact(c.id)} style={{ background: '#FEE2E2', border: 'none', borderRadius: '10px', padding: '0 15px', cursor: 'pointer', color: 'var(--accent)', flexShrink: 0 }}>
                    <Trash2 size={18} />
                  </button>
                )}
              </div>
            ))}
            <button onClick={addContact} style={{ background: 'none', border: '1px dashed #cbd5e1', padding: '12px', borderRadius: '10px', cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '8px', color: '#64748b', fontWeight: '500' }}>
              <Plus size={18} /> הוסף איש קשר נוסף
            </button>
          </div>
        </div>

        <div style={{ height: '1px', background: '#f1f5f9', margin: '2rem 0' }} />

        {/* מצב חופשה */}
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '2rem' }}>
          <div style={{ display: 'flex', gap: '15px' }}>
            <div className="btn-icon-wrapper" style={{ background: '#F1F5F9', width: '40px', height: '40px', marginBottom: 0 }}>
              <Shield size={20} color="#64748b" />
            </div>
            <div>
              <h4 style={{ margin: 0, fontSize: '1.1rem' }}>מצב חופשה</h4>
              <p style={{ margin: 0, fontSize: '0.9rem', color: '#64748b' }}>השהיית התראות באופן זמני</p>
            </div>
          </div>
          <input type="checkbox" checked={isAway} onChange={e => setIsAway(e.target.checked)} style={{ width: '24px', height: '24px', accentColor: 'var(--accent)', cursor: 'pointer' }} />
        </div>

        {/* עוצמת שמע */}
        <div>
          <div style={{ display: 'flex', gap: '15px', marginBottom: '1rem', alignItems: 'center' }}>
            <div className="btn-icon-wrapper" style={{ background: '#F1F5F9', width: '40px', height: '40px', marginBottom: 0 }}>
              <Volume2 size={20} color="#64748b" />
            </div>
            <h4 style={{ margin: 0, fontSize: '1.1rem' }}>עוצמת שמע — {volume}%</h4>
          </div>
          <input
            type="range" min="0" max="100" value={volume}
            onChange={e => setVolume(Number(e.target.value))}
            style={{ width: '100%', height: '6px', background: '#e2e8f0', borderRadius: '10px', accentColor: 'var(--primary)' }}
          />
        </div>

        <button className="btn-primary" onClick={saveSettings} disabled={saving} style={{ marginTop: '3rem', width: '100%', opacity: saving ? 0.7 : 1 }}>
          {saving ? 'שומר...' : 'שמור שינויים בקופסה'}
        </button>
      </div>
    </div>
  );
};

// ─────────────────────────────────────────────────────────────────────────────
// PersonalAreaPage — אזור אישי לבן משפחה
// מציג: סטטוס קופסה, כפתור אישור מרחוק, והיסטוריה מסוכמת
// ─────────────────────────────────────────────────────────────────────────────
const PersonalAreaPage = () => {
  const navigate = useNavigate();
  const boxData = useBoxStatus(true);

  // מיפוי מצבים לצבעים ולטקסט
  const statusMap = {
    IDLE: { color: '#16A34A', bg: '#F0FDF4', label: '✅ הכל תקין — אין צורך בפעולה' },
    ALARM_RINGING: { color: '#D97706', bg: '#FFFBEB', label: '🔔 האזעקה מופעלת — זמן מנה!' },
    WAITING_FINGERPRINT: { color: '#EA580C', bg: '#FFF7ED', label: '👆 מחכה לאימות טביעת אצבע' },
    ROBOT_DEPLOYED: { color: '#7C3AED', bg: '#F5F3FF', label: '🤖 הרובוט נשלח לחפש את המשתמש' },
    DISPENSING: { color: '#0891B2', bg: '#ECFEFF', label: '💊 מנפק תרופה כעת' },
    CALLING_TWILIO: { color: '#DC2626', bg: '#FEF2F2', label: '📞 מתקשר לחירום!' },
    ERROR: { color: '#DC2626', bg: '#FEF2F2', label: '❌ שגיאה בקופסה' },
    Offline: { color: '#94a3b8', bg: '#F8FAFC', label: '📵 הקופסה אינה מחוברת' },
  };
  const st = statusMap[boxData.status] || statusMap.Offline;

  return (
    <div style={{ maxWidth: '700px', margin: '2rem auto', padding: '1.5rem' }}>
      <button onClick={() => navigate('/dashboard')} style={{ background: 'none', border: 'none', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '2rem', color: '#64748b', fontWeight: '500' }}>
        <ChevronLeft size={20} /> חזרה לראשי
      </button>

      <div style={{ display: 'flex', alignItems: 'center', gap: '12px', marginBottom: '2rem' }}>
        <div style={{ background: '#E3F2FD', padding: '10px', borderRadius: '12px' }}>
          <User size={28} color="var(--primary)" />
        </div>
        <div>
          <h2 style={{ margin: 0, fontSize: '1.8rem' }}>אזור אישי</h2>
          <p style={{ margin: 0, color: '#64748b', fontSize: '0.9rem' }}>סקירה ושליטה מרחוק עבור בני המשפחה</p>
        </div>
      </div>

      {/* ── כרזת אישור מרחוק — המרכיב החדש המרכזי ── */}
      <FingerprintWaitingBanner boxData={boxData} />

      {/* ── כרטיס סטטוס ── */}
      <div className="card" style={{ marginBottom: '1.5rem', background: st.bg, border: `1px solid ${st.color}22` }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', flexWrap: 'wrap', gap: '1rem' }}>
          <div>
            <p style={{ margin: '0 0 4px 0', fontSize: '0.8rem', color: '#64748b' }}>מצב מכשיר בזמן אמת</p>
            <p style={{ margin: 0, fontSize: '1.2rem', fontWeight: '700', color: st.color }}>{st.label}</p>
          </div>
          <div style={{ display: 'flex', gap: '1.5rem' }}>
            <div style={{ textAlign: 'center' }}>
              <p style={{ margin: 0, fontSize: '0.75rem', color: '#94a3b8' }}>עוצמת שמע</p>
              <p style={{ margin: 0, fontWeight: '700', color: 'var(--primary)' }}>{boxData.volume}%</p>
            </div>
            <div style={{ textAlign: 'center' }}>
              <p style={{ margin: 0, fontSize: '0.75rem', color: '#94a3b8' }}>מצב חופשה</p>
              <p style={{ margin: 0, fontWeight: '700', color: boxData.isAway ? '#D97706' : '#16A34A' }}>{boxData.isAway ? 'פעיל' : 'כבוי'}</p>
            </div>
            <div style={{ textAlign: 'center' }}>
              <p style={{ margin: 0, fontSize: '0.75rem', color: '#94a3b8' }}>חיבור</p>
              <p style={{ margin: 0, fontWeight: '700', color: boxData.online ? '#16A34A' : '#DC2626' }}>
                {boxData.online ? '✅ מחובר' : '❌ מנותק'}
              </p>
            </div>
          </div>
        </div>
      </div>

      {/* ── הסבר הפיצ'ר ── */}
      <div className="card" style={{ background: '#EFF6FF', border: '1px solid #BFDBFE' }}>
        <div style={{ display: 'flex', gap: '12px', alignItems: 'flex-start' }}>
          <Bell size={22} color="#1D4ED8" style={{ marginTop: '2px', flexShrink: 0 }} />
          <div>
            <h4 style={{ margin: '0 0 0.5rem 0', color: '#1E40AF' }}>מה זה "אישור מרחוק"?</h4>
            <p style={{ margin: 0, color: '#1E3A8A', fontSize: '0.9rem', lineHeight: 1.6 }}>
              כאשר הקופסה ממתינה לטביעת אצבע (למשל, ידיים רטובות, פציעה, או קשיי תנועה), בן משפחה מורשה
              יכול ללחוץ על "אשר עבורי" כדי לאשר את שחרור התרופה מרחוק. הקופסה תפתח את התא ללא צורך
              בטביעת אצבע פיזית. כל אישור מרחוק נרשם ביומן המערכת.
            </p>
          </div>
        </div>
      </div>
    </div>
  );
};

// ─────────────────────────────────────────────────────────────────────────────
// App — הוספת ה-route החדש + שיתוף boxData עם ה-Header
// ─────────────────────────────────────────────────────────────────────────────
function AppInner() {
  // polling גלובלי אחד לכל האפליקציה — מציג אינדיקטור בראש הדף
  const boxData = useBoxStatus(true);
  return (
    <div style={{ minHeight: '100vh', paddingBottom: '2rem' }}>
      <Header boxOnline={boxData.online} />
      {/* כרזת אישור מרחוק גלובלית — מוצגת בכל עמוד */}
      {boxData.waitingFingerprint && (
        <div style={{ maxWidth: '1000px', margin: '0 auto', padding: '1rem 1.5rem 0' }}>
          <FingerprintWaitingBanner boxData={boxData} />
        </div>
      )}
      <Routes>
        <Route path="/" element={<LandingPage />} />
        <Route path="/login" element={<LoginPage />} />
        <Route path="/dashboard" element={<DashboardPage />} />
        <Route path="/upload" element={<UploadPage />} />
        <Route path="/settings" element={<SettingsPage />} />
        <Route path="/personal" element={<PersonalAreaPage />} />
      </Routes>
    </div>
  );
}

function App() {
  return (
    <Router>
      <AppInner />
    </Router>
  );
}

export default App;
