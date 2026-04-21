import React, { useState, useEffect } from 'react';
import { BrowserRouter as Router, Routes, Route, useNavigate } from 'react-router-dom';
import { 
  Bot, Upload, Settings, User, Activity, Shield, ArrowRight, FileText, 
  ChevronLeft, Volume2, Phone, AlertTriangle, Heart, Users, CheckCircle, Clock, Plus, Trash2, AlertOctagon
} from 'lucide-react';
import './index.css';

// --- Header  ---
const Header = () => {
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
      <div style={{ fontSize: '0.9rem', color: '#64748b' }}>גרסה 2.0</div>
    </header>
  );
};


// --- Landing Page ---
const LandingPage = () => {
  const navigate = useNavigate();
  return (
    <div style={{ maxWidth: '1200px', margin: '0 auto', padding: '4rem 1.5rem', textAlign: 'center' }}>
      <div style={{ marginBottom: '5rem' }}>
        <span style={{ background: '#E3F2FD', color: 'var(--primary)', padding: '6px 16px', borderRadius: '20px', fontSize: '0.9rem', fontWeight: '600' }}>פרויקט גמר חדשני</span>
        <h1 style={{ fontSize: '3.5rem', margin: '1.5rem 0', lineHeight: 1.1, color: '#0f172a' }}>
          החבר הבריאותי שלך<br/><span style={{ color: 'var(--primary)' }}>לשקט נפשי אמיתי</span>
        </h1>
        <p style={{ fontSize: '1.25rem', color: '#64748b', maxWidth: '700px', margin: '0 auto 2rem auto' }}>
          מערכת חכמה המשלבת רובוטיקה ובינה מלאכותית כדי למנוע טעויות בנטילת תרופות ולהציל חיים.
        </p>
        <button className="btn-primary" onClick={() => navigate('/login')} style={{ fontSize: '1.1rem', padding: '16px 40px' }}>
          כניסה למערכת <ArrowRight size={20} />
        </button>
      </div>

      <div style={{ marginBottom: '5rem' }}>
        <h2 style={{ fontSize: '2rem', marginBottom: '2rem', color: '#1e293b' }}>למה אנחנו כאן? <span style={{color: 'var(--accent)'}}>הנתונים מדברים בעד עצמם</span></h2>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(250px, 1fr))', gap: '2rem', textAlign: 'right' }}>
          
          <div className="card">
            <div className="btn-icon-wrapper" style={{ background: '#FEE2E2' }}>
              <Activity size={24} color="var(--accent)" />
            </div>
            <h3 style={{ fontSize: '2rem', margin: '0 0 0.5rem 0', color: 'var(--accent)' }}>38%</h3>
            <p style={{ color: '#64748b', fontWeight: '500' }}>מהקשישים בישראל</p>
            <p style={{ fontSize: '0.9rem', color: '#94a3b8' }}>נוטלים מעל 5 תרופות שונות ביום, מה שמגדיל דרמטית את הסיכון לטעויות.</p>
          </div>

          <div className="card">
            <div className="btn-icon-wrapper" style={{ background: '#FEF3C7' }}>
              <AlertTriangle size={24} color="#D97706" />
            </div>
            <h3 style={{ fontSize: '2rem', margin: '0 0 0.5rem 0', color: '#D97706' }}>27%</h3>
            <p style={{ color: '#64748b', fontWeight: '500' }}>טעויות תחת השגחה</p>
            <p style={{ fontSize: '0.9rem', color: '#94a3b8' }}>מהדיירים בבתי אבות חווים טעויות במתן תרופות, למרות הצוות המקצועי.</p>
          </div>

          <div className="card">
            <div className="btn-icon-wrapper" style={{ background: '#F1F5F9' }}>
              <Heart size={24} color="#64748b" />
            </div>
            <h3 style={{ fontSize: '2rem', margin: '0 0 0.5rem 0', color: '#64748b' }}>125,000</h3>
            <p style={{ color: '#64748b', fontWeight: '500' }}>מקרי מוות בשנה</p>
            <p style={{ fontSize: '0.9rem', color: '#94a3b8' }}>בארה"ב בלבד, כתוצאה ישירה מאי-הקפדה על נטילת תרופות נכונה.</p>
          </div>

          <div className="card">
            <div className="btn-icon-wrapper" style={{ background: '#E0F2FE' }}>
              <Users size={24} color="var(--primary)" />
            </div>
            <h3 style={{ fontSize: '2rem', margin: '0 0 0.5rem 0', color: 'var(--primary)' }}>57%</h3>
            <p style={{ color: '#64748b', fontWeight: '500' }}>קושי של בני המשפחה</p>
            <p style={{ fontSize: '0.9rem', color: '#94a3b8' }}>מהמטפלים העיקריים מדווחים על קושי ונטל נפשי בניהול התרופות של יקיריהם.</p>
          </div>

        </div>
      </div>

      <div style={{ background: 'var(--primary)', borderRadius: '24px', padding: '4rem 2rem', color: 'white' }}>
        <h2 style={{ fontSize: '2rem', marginBottom: '3rem' }}>המהפכה של MediMate</h2>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '3rem' }}>
          <div style={{ textAlign: 'center' }}>
            <div style={{ background: 'rgba(255,255,255,0.1)', width: '80px', height: '80px', borderRadius: '50%', display: 'flex', alignItems: 'center', justifyContent: 'center', margin: '0 auto 1.5rem auto' }}>
              <CheckCircle size={40} color="#4ADE80" />
            </div>
            <h3 style={{ fontSize: '2.5rem', margin: 0, fontWeight: '700' }}>99%</h3>
            <p style={{ fontSize: '1.2rem', opacity: 0.9 }}>שיפור בהיענות לטיפול</p>
          </div>
          <div style={{ textAlign: 'center' }}>
            <div style={{ background: 'rgba(255,255,255,0.1)', width: '80px', height: '80px', borderRadius: '50%', display: 'flex', alignItems: 'center', justifyContent: 'center', margin: '0 auto 1.5rem auto' }}>
              <Shield size={40} color="#60A5FA" />
            </div>
            <h3 style={{ fontSize: '2.5rem', margin: 0, fontWeight: '700' }}>0</h3>
            <p style={{ fontSize: '1.2rem', opacity: 0.9 }}>טעויות קריטיות</p>
          </div>
          <div style={{ textAlign: 'center' }}>
            <div style={{ background: 'rgba(255,255,255,0.1)', width: '80px', height: '80px', borderRadius: '50%', display: 'flex', alignItems: 'center', justifyContent: 'center', margin: '0 auto 1.5rem auto' }}>
              <Clock size={40} color="#F472B6" />
            </div>
            <h3 style={{ fontSize: '2.5rem', margin: 0, fontWeight: '700' }}>24/7</h3>
            <p style={{ fontSize: '1.2rem', opacity: 0.9 }}>שקט נפשי למשפחה</p>
          </div>
        </div>
      </div>
    </div>
  );
};

// --- Login Page ---
const LoginPage = () => {
  const navigate = useNavigate();
  return (
    <div style={{ minHeight: '80vh', display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '1rem' }}>
      <div className="card" style={{ width: '100%', maxWidth: '420px', padding: '3rem' }}>
        <div style={{ textAlign: 'center', marginBottom: '2rem' }}>
          <h2 style={{ fontSize: '1.8rem', marginBottom: '0.5rem' }}>ברוכים השבים</h2>
          <p style={{ color: '#64748b' }}>הכנס את פרטי המשתמש שלך</p>
        </div>
        <form onSubmit={(e) => { e.preventDefault(); navigate('/dashboard'); }} style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem' }}>
          <div>
            <label style={{ display: 'block', marginBottom: '8px', fontWeight: '500', fontSize: '0.9rem' }}>אימייל</label>
            <input type="email" placeholder="name@example.com" required />
          </div>
          <div>
            <label style={{ display: 'block', marginBottom: '8px', fontWeight: '500', fontSize: '0.9rem' }}>סיסמה</label>
            <input type="password" placeholder="••••••••" required />
          </div>
          <button type="submit" className="btn-primary" style={{ marginTop: '1rem' }}>כניסה למערכת</button>
        </form>
      </div>
    </div>
  );
};

// --- Dashboard ---
const DashboardPage = () => {
  const navigate = useNavigate();
  const [boxStatus, setBoxStatus] = useState({ isTaken: false, boxStatus: "Offline" });

  // האזנה לשינויים מה-ESP32 בזמן אמת
  useEffect(() => {
    const fetchBoxStatus = async () => {
      try {
        const response = await fetch('http://192.168.1.100/api/box-status'); // Replace with your ESP32 server IP
        if (response.ok) {
          const data = await response.json();
          setBoxStatus(data);
        }
      } catch (error) {
        console.error('Error fetching box status:', error);
      }
    };

    // Fetch immediately
    fetchBoxStatus();

    // Set up polling every 5 seconds
    const interval = setInterval(fetchBoxStatus, 5000);

    return () => clearInterval(interval);
  }, []);
   // נתונים מדומים להיסטוריית תרופות
   const medicationHistory = [
    { id: 1, name: 'אספירין', time: '08:00', status: 'ok', msg: 'נלקח בזמן' },
    { id: 2, name: 'ויטמין D', time: '08:00', status: 'ok', msg: 'נלקח בזמן' },
    { id: 3, name: 'לחץ דם', time: '12:00', status: 'late', msg: 'נלקח באיחור של שעתיים' },
    { id: 4, name: 'אומפרדקס', time: '14:00', status: 'danger', msg: 'חריגת יתר! לא נלקח' }, 
  ];

  const hasCriticalWarning = medicationHistory.some(m => m.status === 'danger');

  return (
    <div style={{ maxWidth: '1000px', margin: '0 auto', padding: '3rem 1.5rem' }}>
      
      {hasCriticalWarning && (
        <div style={{ background: '#FEF2F2', border: '1px solid #FECACA', borderRadius: '16px', padding: '1.5rem', marginBottom: '2rem', display: 'flex', gap: '1rem', alignItems: 'start' }}>
          <div style={{ background: '#FCA5A5', padding: '10px', borderRadius: '50%' }}>
            <AlertOctagon color="#7F1D1D" size={24} />
          </div>
          <div>
            <h3 style={{ color: '#991B1B', margin: '0 0 0.5rem 0' }}>התראת בטיחות חמורה</h3>
            <p style={{ color: '#7F1D1D', margin: 0 }}>זוהתה חריגה בנטילת התרופות היום (אומפרדקס). המערכת שלחה התראה לאנשי הקשר לחירום ונעל את הקופסה למניעת מינון כפול.</p>
          </div>
        </div>
      )}

      <div style={{ marginBottom: '3rem' }}>
        <h2 style={{ fontSize: '2rem', margin: 0 }}>שלום, רחל! 👋</h2>
        <p style={{ color: '#64748b', marginTop: '0.5rem' }}>הנה תמונת המצב שלך להיום</p>
      </div>
      
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(350px, 1fr))', gap: '2rem', marginBottom: '3rem' }}>
        <div className="card hover-card" onClick={() => navigate('/upload')} style={{ cursor: 'pointer', position: 'relative', overflow: 'hidden' }}>
          <div style={{ position: 'absolute', top: 0, right: 0, width: '6px', height: '100%', background: 'var(--primary)' }}></div>
          <div style={{ display: 'flex', justifyContent: 'space-between' }}>
            <div>
              <div className="btn-icon-wrapper" style={{ background: '#E3F2FD' }}>
                <Upload size={24} color="var(--primary)" />
              </div>
              <h3 style={{ fontSize: '1.4rem', marginBottom: '0.5rem' }}>העלאת מרשמים</h3>
              <p style={{ color: '#64748b' }}>סרוק מרשם חדש והמערכת<br/>תעדכן את הקופסה אוטומטית.</p>
            </div>
            <ArrowRight color="#cbd5e1" />
          </div>
        </div>

        <div className="card hover-card" onClick={() => navigate('/settings')} style={{ cursor: 'pointer', position: 'relative', overflow: 'hidden' }}>
          <div style={{ position: 'absolute', top: 0, right: 0, width: '6px', height: '100%', background: 'var(--accent)' }}></div>
          <div style={{ display: 'flex', justifyContent: 'space-between' }}>
            <div>
              <div className="btn-icon-wrapper" style={{ background: '#FEE2E2' }}>
                <Settings size={24} color="var(--accent)" />
              </div>
              <h3 style={{ fontSize: '1.4rem', marginBottom: '0.5rem' }}>הגדרות וחירום</h3>
              <p style={{ color: '#64748b' }}>ניהול אנשי קשר, התראות<br/>ומצב חופשה.</p>
            </div>
            <ArrowRight color="#cbd5e1" />
          </div>
        </div>
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
                {item.status === 'ok' ? <CheckCircle color="#16A34A" /> : 
                 item.status === 'late' ? <Clock color="#D97706" /> : 
                 <AlertTriangle color="#DC2626" />}
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

// --- Upload Page ---
const UploadPage = () => {
  const navigate = useNavigate();
  const [loading, setLoading] = useState(false);

  const handleUpload = () => {
    setLoading(true);
    setTimeout(() => {
      setLoading(false);
      alert('הצלחה! המרשם נותח והקופסה עודכנה.');
      navigate('/dashboard');
    }, 2500);
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
        <p style={{ color: '#64748b', marginBottom: '3rem' }}>צלם את המרשם או גרור קובץ לכאן</p>
        <button className="btn-primary" onClick={handleUpload} disabled={loading} style={{ width: '100%', opacity: loading ? 0.7 : 1 }}>
          {loading ? 'מנתח נתונים...' : 'העלה ונתח'}
        </button>
      </div>
    </div>
  );
};

// --- Settings Page (מאוחד: ESP32 + אנשי קשר) ---
const SettingsPage = () => {
  const navigate = useNavigate();
  
  // -- State ל-ESP32 (מצב חופשה) --
  const [isAway, setIsAway] = useState(false);
  
  // -- State לאנשי קשר --
  const [contacts, setContacts] = useState([
    { id: 1, name: 'בן (הבן שלי)', phone: '050-1234567' }
  ]);

  // 1. ESP32: טעינת מצב חופשה ראשוני
  useEffect(() => {
    const fetchAwayStatus = async () => {
      try {
        const response = await fetch('http://192.168.1.100/api/box-status'); // Replace with your ESP32 server IP
        if (response.ok) {
          const data = await response.json();
          if (data.isAway !== undefined) setIsAway(data.isAway);
        }
      } catch (error) {
        console.error('Error fetching away status:', error);
      }
    };

    fetchAwayStatus();
  }, []);

  // 2. ESP32: פונקציה לשמירת מצב חופשה
  const toggleAwayMode = async (newValue) => {
    setIsAway(newValue);
    try {
      const response = await fetch('http://192.168.1.100/api/box-status', { // Replace with your ESP32 server IP
        method: 'PUT',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({ isAway: newValue }),
      });

      if (response.ok) {
        console.log("עודכן בהצלחה ב-ESP32!");
      } else {
        throw new Error('Failed to update');
      }
    } catch (err) {
      console.error("שגיאה בעדכון:", err);
      alert("שגיאת תקשורת עם הקופסה");
    }
  };

  // 3. פונקציות ניהול אנשי קשר
  const addContact = () => {
    const newId = contacts.length + 1;
    setContacts([...contacts, { id: newId, name: '', phone: '' }]);
  };

  const removeContact = (id) => {
    setContacts(contacts.filter(c => c.id !== id));
  };

  return (
    <div style={{ maxWidth: '600px', margin: '2rem auto', padding: '1.5rem' }}>
      <button onClick={() => navigate('/dashboard')} style={{ background: 'none', border: 'none', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '2rem', color: '#64748b', fontWeight: '500' }}>
        <ChevronLeft size={20} /> חזרה לראשי
      </button>
      
      <div className="card">
        <h2 style={{ fontSize: '1.8rem', marginBottom: '2.5rem', borderBottom: '1px solid #f1f5f9', paddingBottom: '1rem' }}>הגדרות</h2>

        {/* --- חלק 1: אנשי קשר --- */}
        <div style={{ marginBottom: '2rem' }}>
          <div style={{ display: 'flex', gap: '15px', marginBottom: '1rem' }}>
             <div className="btn-icon-wrapper" style={{ background: '#FEE2E2', width: '40px', height: '40px', marginBottom: 0 }}>
              <Phone size={20} color="var(--accent)" />
            </div>
            <h4 style={{ margin: 0, fontSize: '1.1rem', lineHeight: '40px' }}>אנשי קשר לחירום</h4>
          </div>
          
          <div style={{ display: 'flex', flexDirection: 'column', gap: '15px' }}>
            {contacts.map((contact) => (
              <div key={contact.id} style={{ display: 'flex', gap: '10px' }}>
                <input type="text" placeholder="שם מלא" defaultValue={contact.name} />
                <input type="text" placeholder="טלפון" defaultValue={contact.phone} />
                {contacts.length > 1 && (
                  <button onClick={() => removeContact(contact.id)} style={{ background: '#FEE2E2', border: 'none', borderRadius: '10px', padding: '0 15px', cursor: 'pointer', color: 'var(--accent)' }}>
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

        <div style={{ height: '1px', background: '#f1f5f9', margin: '2rem 0' }}></div>

        {/* --- חלק 2: מצב חופשה (ESP32) --- */}
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
          <input 
            type="checkbox" 
            checked={isAway} 
            onChange={(e) => toggleAwayMode(e.target.checked)} 
            style={{ width: '24px', height: '24px', accentColor: 'var(--accent)' }} 
          />
        </div>

        <div>
          <div style={{ display: 'flex', gap: '15px', marginBottom: '1rem' }}>
             <div className="btn-icon-wrapper" style={{ background: '#F1F5F9', width: '40px', height: '40px', marginBottom: 0 }}>
              <Volume2 size={20} color="#64748b" />
            </div>
            <h4 style={{ margin: 0, fontSize: '1.1rem', lineHeight: '40px' }}>עוצמת שמע</h4>
          </div>
          <input type="range" style={{ width: '100%', height: '6px', background: '#e2e8f0', borderRadius: '10px', accentColor: 'var(--primary)' }} />
        </div>

        <button className="btn-primary" onClick={() => navigate('/dashboard')} style={{ marginTop: '3rem', width: '100%' }}>
          שמור שינויים
        </button>
      </div>
    </div>
  );
};

// --- App Router ---
function App() {
  return (
    <Router>
      <div style={{ minHeight: '100vh', paddingBottom: '2rem' }}>
        <Header />
        <Routes>
          <Route path="/" element={<LandingPage />} />
          <Route path="/login" element={<LoginPage />} />
          <Route path="/dashboard" element={<DashboardPage />} />
          <Route path="/upload" element={<UploadPage />} />
          <Route path="/settings" element={<SettingsPage />} />
        </Routes>
      </div>
    </Router>
  );
}

export default App;